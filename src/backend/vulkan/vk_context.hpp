#pragma once

// Vulkan headers stay inside src/backend/vulkan (design rule: API-specific
// headers never leak into the public interface).
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

#include "isc/core/device_caps.hpp"

namespace isc::vk {

const char* result_string(VkResult r);

// Instance + physical/logical device + one compute-capable queue. Headless:
// the layer never presents; the host owns the swapchain.
struct Context {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    std::uint32_t timestamp_valid_bits = 0;
    std::uint32_t instance_api = 0;
    std::uint32_t largest_heap = 0;
    bool has_memory_budget = false;
    VkPhysicalDeviceProperties props{};
    VkPhysicalDeviceMemoryProperties memory{};
    DeviceCaps caps;

    Context() = default;
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    ~Context() { destroy(); }

    bool init(bool validation, int device_index, std::string* error);
    void destroy();

    // Refreshes caps.budget_bytes from VK_EXT_memory_budget (no-op if absent).
    void refresh_budget();
};

// Number of validation-layer errors reported since process start.
std::uint32_t validation_error_count();

}  // namespace isc::vk
