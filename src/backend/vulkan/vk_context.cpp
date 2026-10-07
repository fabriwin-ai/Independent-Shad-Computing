#include "backend/vulkan/vk_context.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace isc::vk {
namespace {

std::atomic<std::uint32_t> g_validation_errors{0};

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::fprintf(stderr, "[vulkan] %s\n", data && data->pMessage ? data->pMessage : "(null)");
    }
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++g_validation_errors;
    return VK_FALSE;
}

bool has_layer(const char* name) {
    std::uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0) return true;
    return false;
}

bool has_instance_extension(const char* name) {
    std::uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

bool has_device_extension(VkPhysicalDevice pd, const char* name) {
    std::uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &n, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

// Returns a compute-capable queue family, preferring a compute-only family
// (async compute) when the device has one. -1 if none.
int pick_queue_family(VkPhysicalDevice pd, bool& async_compute, std::uint32_t& ts_bits) {
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, fams.data());
    int any = -1, dedicated = -1;
    for (std::uint32_t i = 0; i < n; ++i) {
        if (!(fams[i].queueFlags & VK_QUEUE_COMPUTE_BIT) || fams[i].queueCount == 0) continue;
        if (any < 0) any = static_cast<int>(i);
        if (!(fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && dedicated < 0) dedicated = static_cast<int>(i);
    }
    const int chosen = dedicated >= 0 ? dedicated : any;
    async_compute = dedicated >= 0;
    ts_bits = chosen >= 0 ? fams[static_cast<std::uint32_t>(chosen)].timestampValidBits : 0;
    return chosen;
}

int device_score(const VkPhysicalDeviceProperties& p) {
    switch (p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 1000;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 500;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 100;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 10;
        default: return 1;
    }
}

}  // namespace

std::uint32_t validation_error_count() { return g_validation_errors.load(); }

const char* result_string(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        default: return "VkResult(other)";
    }
}

bool Context::init(bool validation, int device_index, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        destroy();
        return false;
    };

    // ---- instance -----------------------------------------------------------
    std::uint32_t loader_api = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loader_api);  // Vulkan 1.1+ loader
    if (loader_api < VK_API_VERSION_1_1) return fail("Vulkan loader older than 1.1");
    instance_api = loader_api < VK_API_VERSION_1_3 ? loader_api : VK_API_VERSION_1_3;

    std::vector<const char*> layers;
    std::vector<const char*> extensions;
    if (validation) {
        if (has_layer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        } else {
            std::fprintf(stderr, "[vulkan] validation requested but VK_LAYER_KHRONOS_validation is not installed\n");
        }
        if (has_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Independent-Shad-Computing";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName = "isc";
    app.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion = instance_api;

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    ici.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) return fail(std::string("vkCreateInstance failed: ") + result_string(r));

    if (!extensions.empty()) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mci.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mci.pfnUserCallback = debug_callback;
            create(instance, &mci, nullptr, &messenger);
        }
    }

    // ---- physical device ----------------------------------------------------
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    if (count == 0) return fail("no Vulkan physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    int best = -1, best_score = -1, best_family = -1;
    bool best_async = false;
    std::uint32_t best_ts = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i], &p);
        bool async = false;
        std::uint32_t ts = 0;
        const int family = pick_queue_family(devices[i], async, ts);
        if (family < 0 || p.apiVersion < VK_API_VERSION_1_1) continue;
        const int score = device_index >= 0 ? (static_cast<int>(i) == device_index ? 1 : -1) : device_score(p);
        if (score > best_score) {
            best = static_cast<int>(i);
            best_score = score;
            best_family = family;
            best_async = async;
            best_ts = ts;
        }
    }
    if (best < 0 || best_score < 0) return fail("no suitable Vulkan 1.1+ device with a compute queue");
    physical = devices[static_cast<std::uint32_t>(best)];
    queue_family = static_cast<std::uint32_t>(best_family);
    timestamp_valid_bits = best_ts;

    vkGetPhysicalDeviceProperties(physical, &props);
    vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    has_memory_budget = has_device_extension(physical, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    // ---- logical device -----------------------------------------------------
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    std::vector<const char*> device_exts;
    if (has_memory_budget) device_exts.push_back(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<std::uint32_t>(device_exts.size());
    dci.ppEnabledExtensionNames = device_exts.data();
    r = vkCreateDevice(physical, &dci, nullptr, &device);
    if (r != VK_SUCCESS) return fail(std::string("vkCreateDevice failed: ") + result_string(r));
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    // ---- capabilities -------------------------------------------------------
    VkPhysicalDeviceSubgroupProperties subgroup{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceMaintenance3Properties maint3{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};
    maint3.pNext = &subgroup;
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &maint3;
    vkGetPhysicalDeviceProperties2(physical, &props2);

    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceFeatures2 feats2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    const bool v12 = props.apiVersion >= VK_API_VERSION_1_2 && instance_api >= VK_API_VERSION_1_2;
    if (v12) feats2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(physical, &feats2);

    caps = DeviceCaps{};
    caps.name = props.deviceName;
    caps.backend = BackendKind::Vulkan;
    caps.vendor_id = props.vendorID;
    caps.device_id = props.deviceID;
    caps.api_version = props.apiVersion;
    caps.integrated = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    VkDeviceSize largest = 0;
    for (std::uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
        if ((memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && memory.memoryHeaps[h].size > largest) {
            largest = memory.memoryHeaps[h].size;
            largest_heap = h;
        }
    }
    caps.device_local_bytes = largest;
    for (std::uint32_t t = 0; t < memory.memoryTypeCount; ++t) {
        const VkMemoryPropertyFlags f = memory.memoryTypes[t].propertyFlags;
        if (memory.memoryTypes[t].heapIndex == largest_heap && (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            caps.unified_memory = true;
        }
    }
    caps.max_allocation_bytes = maint3.maxMemoryAllocationSize;
    caps.max_workgroup_invocations = props.limits.maxComputeWorkGroupInvocations;
    caps.max_shared_memory_bytes = props.limits.maxComputeSharedMemorySize;
    caps.max_storage_buffer_bytes = props.limits.maxStorageBufferRange;
    caps.subgroup_size = subgroup.subgroupSize;
    caps.shader_float64 = feats2.features.shaderFloat64 == VK_TRUE;
    caps.shader_float16 = v12 && f12.shaderFloat16 == VK_TRUE;
    caps.timeline_semaphore = v12 && f12.timelineSemaphore == VK_TRUE;
    caps.async_compute_queue = best_async;
    caps.timestamp_period_ns = props.limits.timestampPeriod;
    caps.cpu_threads = std::thread::hardware_concurrency();
    refresh_budget();
    return true;
}

void Context::refresh_budget() {
    if (!has_memory_budget || physical == VK_NULL_HANDLE) return;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 mp2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
    mp2.pNext = &budget;
    vkGetPhysicalDeviceMemoryProperties2(physical, &mp2);
    caps.budget_bytes = budget.heapBudget[largest_heap];
}

void Context::destroy() {
    if (device) {
        vkDeviceWaitIdle(device);
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (messenger) {
        auto destroy_fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy_fn) destroy_fn(instance, messenger, nullptr);
        messenger = VK_NULL_HANDLE;
    }
    if (instance) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    physical = VK_NULL_HANDLE;
    queue = VK_NULL_HANDLE;
}

}  // namespace isc::vk
