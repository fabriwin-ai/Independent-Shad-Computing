#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "isc/core/device_caps.hpp"
#include "isc/core/profile.hpp"
#include "isc/core/render_target.hpp"
#include "isc/plan/execution_plan.hpp"

namespace isc {

namespace sched {
class Scheduler;
}

struct StageStats {
    std::string name;
    float weight_in = 0.0f;   // declared / initial weight
    float weight_out = 0.0f;  // weight used for the composite (optimised when gradient ran)
    std::uint32_t iterations = 0;
    double loss_initial = 0.0;
    double loss_final = 0.0;
};

struct MemoryStats {
    std::uint64_t used_bytes = 0;      // live allocations this frame
    std::uint64_t reserved_bytes = 0;  // device memory blocks held by the allocator
    std::uint64_t budget_bytes = 0;
    std::uint32_t blocks = 0;
    std::uint64_t cache_bytes = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t cache_misses = 0;
    std::uint64_t cache_evictions = 0;
};

struct FrameStats {
    FrameIndex frame = 0;
    Lod lod = Lod::Bypass;
    float resolution_scale = 1.0f;
    std::uint32_t work_width = 0;
    std::uint32_t work_height = 0;
    double layer_ms = 0.0;  // wall time spent inside the layer
    double gpu_ms = 0.0;    // GPU timestamp delta (0 for CPU)
    double load = 0.0;      // controller load ratio after this frame
    bool rung_changed = false;
    MemoryStats memory;
    std::vector<StageStats> stages;
};

// Render/compute backend. API-specific headers (vulkan.h, ...) never appear
// in this interface; everything API-specific lives in src/backend/<api>/.
class Backend {
public:
    virtual ~Backend() = default;

    virtual std::string_view name() const = 0;
    virtual const DeviceCaps& caps() const = 0;

    // Apply a (device-tuned) profile: memory budget, block size, cache size.
    virtual void configure(const RenderProfile& profile) = 0;

    // Run one frame. `output` is resized to the input size if necessary.
    virtual bool execute(const plan::ExecutionPlan& plan, const RenderTarget& input, RenderTarget& output,
                         FrameStats& stats, std::string* error) = 0;

    virtual void wait_idle() {}
};

std::unique_ptr<Backend> make_cpu_backend(sched::Scheduler& scheduler);

struct VulkanBackendOptions {
    bool enable_validation = false;  // VK_LAYER_KHRONOS_validation if installed
    int device_index = -1;           // -1 = best device (discrete > integrated)
};

bool vulkan_backend_compiled();
std::unique_ptr<Backend> make_vulkan_backend(const VulkanBackendOptions& options, std::string* error);

}  // namespace isc
