// Vulkan compute backend.
//
// Per frame (one command buffer, one submit, one fence):
//
//   host frame --memcpy--> in (Upload: host-visible, zero-copy on UMA/ReBAR)
//   for each stage:
//     [resample]  src -> work_in              (dynamic resolution)
//     blur_h      work -> tmp
//     detail      work, tmp -> det
//     [reduce]    work, det -> partials       (gradient: edge-energy sums)
//     [optimize]  partials -> params          (gradient descent on the GPU)
//     composite   src, det, params -> out
//   out (Readback: host-visible + HOST_CACHED) --memcpy--> host frame
//
// All buffers come from a byte-budgeted LRU cache backed by a block
// sub-allocator, so steady-state frames perform no Vulkan allocations and
// dynamic-resolution changes reuse recently used sizes.

#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <vector>

#include "backend/vulkan/vk_allocator.hpp"
#include "backend/vulkan/vk_context.hpp"
#include "isc/backend/backend.hpp"
#include "isc/core/resource_cache.hpp"

#include "blur_h.spv.h"
#include "composite.spv.h"
#include "detail.spv.h"
#include "optimize.spv.h"
#include "reduce.spv.h"
#include "resample.spv.h"

namespace isc {
namespace {

using vk::Allocation;
using vk::MemoryUsage;

enum Kernel : std::uint32_t { kResample, kBlurH, kDetail, kReduce, kOptimize, kComposite, kKernelCount };

struct SpirvBlob {
    const unsigned char* data;
    unsigned long long size;
};

const std::array<SpirvBlob, kKernelCount> kSpirv = {{
    {isc_spv_resample, isc_spv_resample_size},
    {isc_spv_blur_h, isc_spv_blur_h_size},
    {isc_spv_detail, isc_spv_detail_size},
    {isc_spv_reduce, isc_spv_reduce_size},
    {isc_spv_optimize, isc_spv_optimize_size},
    {isc_spv_composite, isc_spv_composite_size},
}};

constexpr std::uint32_t kBindings = 4;
constexpr std::uint32_t kPushBytes = 64;
constexpr std::uint32_t kTile = 16;
constexpr std::uint32_t kMaxSetsPerFrame = 256;

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    Allocation alloc;
    VkDeviceSize size = 0;
    MemoryUsage usage = MemoryUsage::DeviceLocal;
};

struct PushResample {
    std::uint32_t sw, sh, dw, dh;
};
struct PushBlur {
    std::uint32_t w, h, radius;
};
struct PushReduce {
    std::uint32_t w, h;
};
struct PushOptimize {
    std::uint32_t count, iterations;
    float weight, learning_rate, target, weight_min, weight_max, inv_n, tolerance;
};
struct PushComposite {
    std::uint32_t w, h, dw, dh;
};

std::uint32_t groups(std::uint32_t n) { return (n + kTile - 1) / kTile; }

class VulkanBackend final : public Backend {
public:
    ~VulkanBackend() override { shutdown(); }

    bool init(const VulkanBackendOptions& o, std::string* error) {
        if (!ctx_.init(o.enable_validation, o.device_index, error)) return false;
        allocator_.init(ctx_);
        cache_.set_evict_callback([this](Buffer& b) { destroy_buffer(b); });
        return create_objects(error);
    }

    std::string_view name() const override { return "vulkan"; }
    const DeviceCaps& caps() const override { return ctx_.caps; }

    void configure(const RenderProfile& p) override {
        allocator_.configure(p.memory_block_bytes(), p.memory_budget_bytes());
        cache_headroom_ = p.buffer_cache_bytes();
        cache_.set_budget(cache_headroom_);
        allocator_.trim();
    }

    void wait_idle() override {
        if (ctx_.device) vkDeviceWaitIdle(ctx_.device);
    }

    bool execute(const plan::ExecutionPlan& plan, const RenderTarget& input, RenderTarget& output, FrameStats& stats,
                 std::string* error) override {
        if (input.empty()) {
            if (error) *error = "input render target is empty";
            return false;
        }
        if (output.width() != input.width() || output.height() != input.height())
            output.resize(input.width(), input.height());

        stats.lod = plan.lod;
        stats.resolution_scale = plan.resolution_scale;
        stats.work_width = plan.work_width;
        stats.work_height = plan.work_height;
        stats.stages.clear();
        stats.gpu_ms = 0.0;

        if (plan.bypass()) {  // LOD0: no GPU work at all
            std::memcpy(output.data(), input.data(), input.byte_size());
            output.set_ownership(Ownership::Host);
            fill_memory_stats(stats);
            return true;
        }

        const bool ok = run(plan, input, output, stats, error);
        if (!ok) vkDeviceWaitIdle(ctx_.device);  // never recycle buffers the GPU may still touch
        release_frame_buffers();
        vkResetDescriptorPool(ctx_.device, desc_pool_, 0);
        fill_memory_stats(stats);
        return ok;
    }

private:
    // ---------------------------------------------------------------- setup
    bool create_objects(std::string* error) {
        auto check = [&](VkResult r, const char* what) {
            if (r == VK_SUCCESS) return true;
            if (error) *error = std::string(what) + " failed: " + vk::result_string(r);
            return false;
        };

        std::array<VkDescriptorSetLayoutBinding, kBindings> bindings{};
        for (std::uint32_t i = 0; i < kBindings; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dl.bindingCount = kBindings;
        dl.pBindings = bindings.data();
        if (!check(vkCreateDescriptorSetLayout(ctx_.device, &dl, nullptr, &set_layout_), "vkCreateDescriptorSetLayout"))
            return false;

        VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, kPushBytes};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &set_layout_;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pcr;
        if (!check(vkCreatePipelineLayout(ctx_.device, &pl, nullptr, &pipeline_layout_), "vkCreatePipelineLayout"))
            return false;

        VkPipelineCacheCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (!check(vkCreatePipelineCache(ctx_.device, &pci, nullptr, &pipeline_cache_), "vkCreatePipelineCache"))
            return false;

        for (std::uint32_t k = 0; k < kKernelCount; ++k) {
            std::vector<std::uint32_t> code((kSpirv[k].size + 3) / 4);
            std::memcpy(code.data(), kSpirv[k].data, static_cast<std::size_t>(kSpirv[k].size));
            VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            smi.codeSize = static_cast<std::size_t>(kSpirv[k].size);
            smi.pCode = code.data();
            VkShaderModule module = VK_NULL_HANDLE;
            if (!check(vkCreateShaderModule(ctx_.device, &smi, nullptr, &module), "vkCreateShaderModule")) return false;

            VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cpi.stage.module = module;
            cpi.stage.pName = "main";
            cpi.layout = pipeline_layout_;
            const VkResult r = vkCreateComputePipelines(ctx_.device, pipeline_cache_, 1, &cpi, nullptr, &pipelines_[k]);
            vkDestroyShaderModule(ctx_.device, module, nullptr);
            if (!check(r, "vkCreateComputePipelines")) return false;
        }

        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxSetsPerFrame * kBindings};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dp.maxSets = kMaxSetsPerFrame;
        dp.poolSizeCount = 1;
        dp.pPoolSizes = &ps;
        if (!check(vkCreateDescriptorPool(ctx_.device, &dp, nullptr, &desc_pool_), "vkCreateDescriptorPool"))
            return false;

        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cp.queueFamilyIndex = ctx_.queue_family;
        if (!check(vkCreateCommandPool(ctx_.device, &cp, nullptr, &cmd_pool_), "vkCreateCommandPool")) return false;

        VkCommandBufferAllocateInfo cba{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cba.commandPool = cmd_pool_;
        cba.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cba.commandBufferCount = 1;
        if (!check(vkAllocateCommandBuffers(ctx_.device, &cba, &cmd_), "vkAllocateCommandBuffers")) return false;

        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (!check(vkCreateFence(ctx_.device, &fi, nullptr, &fence_), "vkCreateFence")) return false;

        if (ctx_.timestamp_valid_bits > 0) {
            VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qp.queryCount = 2;
            if (vkCreateQueryPool(ctx_.device, &qp, nullptr, &query_pool_) != VK_SUCCESS) query_pool_ = VK_NULL_HANDLE;
        }

        // Small buffer bound to unused descriptor slots so every binding is valid.
        allocator_.configure(32ull << 20, 256ull << 20);
        return create_buffer(256, MemoryUsage::DeviceLocal, dummy_, error);
    }

    void shutdown() {
        if (!ctx_.device) return;
        vkDeviceWaitIdle(ctx_.device);
        release_frame_buffers();
        cache_.clear();
        destroy_buffer(dummy_);
        allocator_.destroy();
        if (query_pool_) vkDestroyQueryPool(ctx_.device, query_pool_, nullptr);
        if (fence_) vkDestroyFence(ctx_.device, fence_, nullptr);
        if (cmd_pool_) vkDestroyCommandPool(ctx_.device, cmd_pool_, nullptr);
        if (desc_pool_) vkDestroyDescriptorPool(ctx_.device, desc_pool_, nullptr);
        for (auto& p : pipelines_)
            if (p) vkDestroyPipeline(ctx_.device, p, nullptr);
        if (pipeline_cache_) vkDestroyPipelineCache(ctx_.device, pipeline_cache_, nullptr);
        if (pipeline_layout_) vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
        if (set_layout_) vkDestroyDescriptorSetLayout(ctx_.device, set_layout_, nullptr);
        ctx_.destroy();
    }

    // -------------------------------------------------------------- buffers
    bool create_buffer(VkDeviceSize size, MemoryUsage usage, Buffer& out, std::string* error) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult r = vkCreateBuffer(ctx_.device, &bi, nullptr, &out.buffer);
        if (r != VK_SUCCESS) {
            if (error) *error = std::string("vkCreateBuffer failed: ") + vk::result_string(r);
            return false;
        }
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(ctx_.device, out.buffer, &req);
        if (!allocator_.allocate(req, usage, out.alloc, error)) {
            vkDestroyBuffer(ctx_.device, out.buffer, nullptr);
            out.buffer = VK_NULL_HANDLE;
            return false;
        }
        r = vkBindBufferMemory(ctx_.device, out.buffer, out.alloc.memory, out.alloc.offset);
        if (r != VK_SUCCESS) {
            if (error) *error = std::string("vkBindBufferMemory failed: ") + vk::result_string(r);
            destroy_buffer(out);
            return false;
        }
        out.size = size;
        out.usage = usage;
        return true;
    }

    void destroy_buffer(Buffer& b) {
        if (b.buffer) vkDestroyBuffer(ctx_.device, b.buffer, nullptr);
        allocator_.free(b.alloc);
        b = Buffer{};
    }

    // Cached buffer of exactly `size` bytes; on budget pressure the cache is
    // flushed and empty blocks returned before retrying once.
    Buffer* acquire(VkDeviceSize size, MemoryUsage usage, std::string* error) {
        const CacheKey key{size, static_cast<std::uint64_t>(usage)};
        Buffer b;
        if (auto cached = cache_.take(key)) {
            b = *cached;
        } else if (!create_buffer(size, usage, b, nullptr)) {
            cache_.clear();
            allocator_.trim();
            if (!create_buffer(size, usage, b, error)) return nullptr;
        }
        frame_buffers_.push_back(std::make_unique<Buffer>(b));
        return frame_buffers_.back().get();
    }

    // The frame's own working set always stays cached: it was resident during
    // the frame anyway, so keeping it costs no extra peak memory, and evicting
    // part of it would force a re-allocation (and fresh page faults) on every
    // steady-state frame. `buffer_cache_mb` is the headroom kept *on top* of
    // that, for neighbouring dynamic-resolution rungs. The allocator's memory
    // budget remains the hard cap (acquire() flushes the cache when it is hit).
    void release_frame_buffers() {
        VkDeviceSize frame_bytes = 0;
        for (const auto& b : frame_buffers_) frame_bytes += b->size;
        cache_.set_budget(cache_headroom_ + frame_bytes);
        for (auto& b : frame_buffers_) cache_.put(CacheKey{b->size, static_cast<std::uint64_t>(b->usage)}, *b, b->size);
        frame_buffers_.clear();
    }

    void fill_memory_stats(FrameStats& stats) const {
        const CacheStats& c = cache_.stats();
        stats.memory.used_bytes = allocator_.used();
        stats.memory.reserved_bytes = allocator_.reserved();
        stats.memory.budget_bytes = allocator_.budget();
        stats.memory.blocks = allocator_.block_count();
        stats.memory.cache_bytes = c.bytes_cached;
        stats.memory.cache_hits = c.hits;
        stats.memory.cache_misses = c.misses;
        stats.memory.cache_evictions = c.evictions;
    }

    // ------------------------------------------------------------ recording
    void barrier(VkPipelineStageFlags src_stage, VkAccessFlags src, VkPipelineStageFlags dst_stage, VkAccessFlags dst) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = src;
        mb.dstAccessMask = dst;
        vkCmdPipelineBarrier(cmd_, src_stage, dst_stage, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    void compute_barrier() {
        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }

    bool dispatch(Kernel k, std::initializer_list<const Buffer*> bufs, const void* push, std::uint32_t push_size,
                  std::uint32_t gx, std::uint32_t gy, std::string* error) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = desc_pool_;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &set_layout_;
        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult r = vkAllocateDescriptorSets(ctx_.device, &ai, &set);
        if (r != VK_SUCCESS) {
            if (error) *error = std::string("vkAllocateDescriptorSets failed: ") + vk::result_string(r);
            return false;
        }
        std::array<VkDescriptorBufferInfo, kBindings> infos{};
        std::array<VkWriteDescriptorSet, kBindings> writes{};
        auto it = bufs.begin();
        for (std::uint32_t i = 0; i < kBindings; ++i) {
            const Buffer* b = (it != bufs.end()) ? *it++ : &dummy_;
            infos[i] = {b->buffer, 0, VK_WHOLE_SIZE};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(ctx_.device, kBindings, writes.data(), 0, nullptr);
        vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines_[k]);
        vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout_, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push);
        vkCmdDispatch(cmd_, gx, gy, 1);
        compute_barrier();
        return true;
    }

    // ----------------------------------------------------------------- frame
    struct StageBuffers {
        Buffer* work_in = nullptr;
        Buffer* tmp = nullptr;
        Buffer* det = nullptr;
        Buffer* partials = nullptr;
        Buffer* params = nullptr;
        Buffer* out = nullptr;
    };

    bool run(const plan::ExecutionPlan& plan, const RenderTarget& input, RenderTarget& output, FrameStats& stats,
             std::string* error) {
        const std::uint32_t W = plan.width, H = plan.height, w = plan.work_width, h = plan.work_height;
        const bool scaled = plan.scaled();
        const std::size_t n = plan.stages.size();
        if (n * 6 > kMaxSetsPerFrame) {
            if (error) *error = "too many artifact stages for one frame";
            return false;
        }
        const VkDeviceSize full = RenderTarget::bytes_for(W, H);
        const VkDeviceSize work = RenderTarget::bytes_for(w, h);
        const std::uint32_t gx = groups(w), gy = groups(h);
        const VkDeviceSize partial_bytes = static_cast<VkDeviceSize>(gx) * gy * 16;

        // ---- resources (capture buffer + per-stage working set) -----------
        auto get = [&](Buffer*& slot, VkDeviceSize size, MemoryUsage usage) {
            slot = acquire(size, usage, error);
            return slot != nullptr;
        };
        Buffer* in = nullptr;
        if (!get(in, full, MemoryUsage::Upload)) return false;
        std::vector<StageBuffers> sb(n);
        for (std::size_t i = 0; i < n; ++i) {
            StageBuffers& s = sb[i];
            const bool last = i + 1 == n;
            if (scaled && !get(s.work_in, work, MemoryUsage::DeviceLocal)) return false;
            if (!get(s.tmp, work, MemoryUsage::DeviceLocal)) return false;
            if (!get(s.det, work, MemoryUsage::DeviceLocal)) return false;
            if (plan.stages[i].gradient && !get(s.partials, partial_bytes, MemoryUsage::DeviceLocal)) return false;
            // params: host writes the declared weight, GPU may optimise it, host reads it back.
            if (!get(s.params, 16, MemoryUsage::Readback)) return false;
            if (!get(s.out, full, last ? MemoryUsage::Readback : MemoryUsage::DeviceLocal)) return false;
        }

        // ---- capture: host -> GPU-visible memory ----------------------------
        std::memcpy(in->alloc.mapped, input.data(), static_cast<std::size_t>(full));
        for (std::size_t i = 0; i < n; ++i) {
            const float p[4] = {plan.stages[i].weight, 0.0f, 0.0f, 0.0f};
            std::memcpy(sb[i].params->alloc.mapped, p, sizeof(p));
        }

        // ---- record -------------------------------------------------------
        vkResetCommandBuffer(cmd_, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd_, &bi);
        if (query_pool_) {
            vkCmdResetQueryPool(cmd_, query_pool_, 0, 2);
            vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, query_pool_, 0);
        }
        barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT);

        for (std::size_t i = 0; i < n; ++i) {
            const plan::StagePlan& sp = plan.stages[i];
            StageBuffers& s = sb[i];
            const Buffer* src = sp.source_stage < 0 ? in : sb[static_cast<std::size_t>(sp.source_stage)].out;
            const Buffer* wk = scaled ? s.work_in : src;

            if (scaled) {
                const PushResample pr{W, H, w, h};
                if (!dispatch(kResample, {src, s.work_in}, &pr, sizeof(pr), gx, gy, error)) return false;
            }
            const PushBlur pb{w, h, sp.radius};
            if (!dispatch(kBlurH, {wk, s.tmp}, &pb, sizeof(pb), gx, gy, error)) return false;
            if (!dispatch(kDetail, {wk, s.tmp, s.det}, &pb, sizeof(pb), gx, gy, error)) return false;
            if (sp.gradient) {
                const PushReduce pr{w, h};
                if (!dispatch(kReduce, {wk, s.det, s.partials}, &pr, sizeof(pr), gx, gy, error)) return false;
                PushOptimize po{};
                po.count = gx * gy;
                po.iterations = sp.iterations;
                po.weight = sp.weight;
                po.learning_rate = sp.learning_rate;
                po.target = sp.target;
                po.weight_min = sp.weight_min;
                po.weight_max = sp.weight_max;
                po.inv_n = static_cast<float>(1.0 / (static_cast<double>(w) * h * 3.0));
                po.tolerance = 1e-6f;
                if (!dispatch(kOptimize, {s.partials, s.params}, &po, sizeof(po), 1, 1, error)) return false;
            }
            const PushComposite pc{W, H, w, h};
            if (!dispatch(kComposite, {src, s.det, s.params, s.out}, &pc, sizeof(pc), groups(W), groups(H), error))
                return false;
        }

        barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                VK_ACCESS_HOST_READ_BIT);
        if (query_pool_) vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_, 1);
        vkEndCommandBuffer(cmd_);

        // ---- submit + wait (synchronous hand-off for CPU-side host frames) --
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd_;
        VkResult r = vkQueueSubmit(ctx_.queue, 1, &si, fence_);
        if (r != VK_SUCCESS) {
            if (error) *error = std::string("vkQueueSubmit failed: ") + vk::result_string(r);
            return false;
        }
        r = vkWaitForFences(ctx_.device, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000);
        vkResetFences(ctx_.device, 1, &fence_);
        if (r != VK_SUCCESS) {
            if (error) *error = std::string("GPU did not finish the frame: ") + vk::result_string(r);
            return false;
        }

        // ---- display: GPU-visible memory -> host ---------------------------
        std::memcpy(output.data(), sb[n - 1].out->alloc.mapped, static_cast<std::size_t>(full));
        output.set_ownership(Ownership::Host);

        for (std::size_t i = 0; i < n; ++i) {
            const plan::StagePlan& sp = plan.stages[i];
            float p[4];
            std::memcpy(p, sb[i].params->alloc.mapped, sizeof(p));
            StageStats st;
            st.name = sp.name;
            st.weight_in = sp.weight;
            st.weight_out = p[0];
            st.iterations = sp.gradient ? static_cast<std::uint32_t>(p[1] + 0.5f) : 0u;
            st.loss_final = sp.gradient ? p[2] : 0.0;
            st.loss_initial = sp.gradient ? p[3] : 0.0;
            stats.stages.push_back(std::move(st));
        }

        if (query_pool_) {
            std::uint64_t ts[2] = {0, 0};
            if (vkGetQueryPoolResults(ctx_.device, query_pool_, 0, 2, sizeof(ts), ts, sizeof(std::uint64_t),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
                const std::uint64_t mask =
                    ctx_.timestamp_valid_bits >= 64 ? ~0ull : ((1ull << ctx_.timestamp_valid_bits) - 1);
                const std::uint64_t dt = ((ts[1] & mask) - (ts[0] & mask)) & mask;
                stats.gpu_ms = static_cast<double>(dt) * ctx_.props.limits.timestampPeriod / 1.0e6;
            }
        }
        return true;
    }

    vk::Context ctx_;
    vk::Allocator allocator_;
    ResourceCache<Buffer> cache_{64ull << 20};
    std::uint64_t cache_headroom_ = 64ull << 20;  // profile buffer_cache_mb
    std::vector<std::unique_ptr<Buffer>> frame_buffers_;
    Buffer dummy_;

    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipelineCache pipeline_cache_ = VK_NULL_HANDLE;
    std::array<VkPipeline, kKernelCount> pipelines_{};
    VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkQueryPool query_pool_ = VK_NULL_HANDLE;
};

}  // namespace

bool vulkan_backend_compiled() { return true; }

std::unique_ptr<Backend> make_vulkan_backend(const VulkanBackendOptions& options, std::string* error) {
    auto backend = std::make_unique<VulkanBackend>();
    if (!backend->init(options, error)) return nullptr;
    return backend;
}

}  // namespace isc
