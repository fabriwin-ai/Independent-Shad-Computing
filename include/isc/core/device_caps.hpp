#pragma once

#include <cstdint>
#include <string>

#include "isc/core/types.hpp"

namespace isc {

// API-neutral description of the device executing the enhancement pipeline.
// Backends fill this from the real device; presets exist for offline tuning
// and tests. Profiles are auto-tuned against it (see profile.hpp).
struct DeviceCaps {
    std::string name = "CPU reference";
    BackendKind backend = BackendKind::Cpu;
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t api_version = 0;  // VK_MAKE_API_VERSION encoding for Vulkan

    bool integrated = false;
    bool unified_memory = false;            // device-local memory is host visible (UMA / ReBAR)
    std::uint64_t device_local_bytes = 0;   // largest device-local heap
    std::uint64_t budget_bytes = 0;         // VK_EXT_memory_budget heap budget, 0 = unknown
    std::uint64_t max_allocation_bytes = 0; // maxMemoryAllocationSize, 0 = unknown

    std::uint32_t max_workgroup_invocations = 0;
    std::uint32_t max_shared_memory_bytes = 0;
    std::uint32_t subgroup_size = 0;
    std::uint64_t max_storage_buffer_bytes = 0;

    bool shader_float16 = false;
    bool shader_float64 = false;
    bool timeline_semaphore = false;
    bool async_compute_queue = false;       // dedicated compute queue family
    float timestamp_period_ns = 0.0f;

    std::uint32_t cpu_threads = 0;

    bool supports(Precision p) const;
    std::string summary() const;
};

DeviceCaps cpu_device_caps();

// Values taken from the gpuinfo/vulkaninfo profile "Intel(R) HD Graphics Gen11"
// (Vulkan 1.3.215, driver 101.x): integrated, single 3.89 GiB shared heap,
// one graphics+compute+transfer queue, fp16 yes, fp64 no, subgroup 8..32.
DeviceCaps intel_hd_graphics_gen11_caps();

}  // namespace isc
