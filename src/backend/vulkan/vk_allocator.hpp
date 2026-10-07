#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backend/vulkan/vk_context.hpp"

namespace isc::vk {

enum class MemoryUsage : std::uint32_t {
    DeviceLocal = 0,  // GPU-only intermediates
    // Host writes, GPU reads (frame import, parameters). Prefers DEVICE_LOCAL|HOST_VISIBLE
    // (UMA / ReBAR = zero copy); write-combined memory is fine for streaming writes.
    Upload = 1,
    // GPU writes, host reads (frame export, optimised parameters). Prefers HOST_CACHED:
    // CPU reads from uncached write-combined memory are an order of magnitude slower
    // (measured on Intel HD Graphics Gen11: ~150 ms vs a few ms for a 720p RGBA32F frame).
    Readback = 2,
};

struct Allocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr;  // host pointer at `offset` (Upload / Readback only)
    std::uint32_t block = 0xFFFFFFFFu;
    bool valid() const { return memory != VK_NULL_HANDLE; }
};

// Budgeted device-memory allocator driven by the render profile:
//   * memory is reserved in blocks of `memory_block_mb` and sub-allocated
//     (first fit, aligned, coalescing free list) - few vkAllocateMemory calls,
//     far below maxMemoryAllocationCount;
//   * requests larger than half a block get a dedicated allocation;
//   * total reserved memory never exceeds `memory_budget_mb` - the request
//     fails instead, and the runtime reacts by lowering the resolution scale.
class Allocator {
public:
    void init(Context& ctx) { ctx_ = &ctx; }
    void configure(VkDeviceSize block_size, VkDeviceSize budget);

    bool allocate(const VkMemoryRequirements& req, MemoryUsage usage, Allocation& out, std::string* error);
    void free(Allocation& a);
    void trim();  // release blocks with no live sub-allocations
    void destroy();

    VkDeviceSize reserved() const { return reserved_; }
    VkDeviceSize used() const { return used_; }
    VkDeviceSize budget() const { return budget_; }
    std::uint32_t block_count() const;

private:
    struct Range {
        VkDeviceSize offset;
        VkDeviceSize size;
    };
    struct Block {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        std::uint32_t type = 0;
        void* mapped = nullptr;
        bool dedicated = false;
        VkDeviceSize used = 0;
        std::vector<Range> free;
    };

    int find_type(std::uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;
    bool new_block(VkDeviceSize size, std::uint32_t type, bool dedicated, std::uint32_t& index, std::string* error);
    static bool carve(Block& b, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& offset);

    Context* ctx_ = nullptr;
    std::vector<Block> blocks_;  // released slots keep memory == VK_NULL_HANDLE
    VkDeviceSize block_size_ = 32ull << 20;
    VkDeviceSize budget_ = 256ull << 20;
    VkDeviceSize reserved_ = 0;
    VkDeviceSize used_ = 0;
};

}  // namespace isc::vk
