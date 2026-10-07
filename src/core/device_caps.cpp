#include "isc/core/device_caps.hpp"

#include <sstream>
#include <thread>

namespace isc {

bool DeviceCaps::supports(Precision p) const {
    switch (p) {
        case Precision::FP16: return shader_float16;
        case Precision::FP32: return true;
        case Precision::FP64: return shader_float64;
    }
    return false;
}

std::string DeviceCaps::summary() const {
    std::ostringstream s;
    s << name << " [" << to_string(backend) << "]";
    if (backend == BackendKind::Vulkan) {
        s << " vk " << (api_version >> 22u) << '.' << ((api_version >> 12u) & 0x3FFu) << '.'
          << (api_version & 0xFFFu);
        s << (integrated ? " integrated" : " discrete");
        s << " heap=" << (device_local_bytes / kMiB) << "MiB";
        if (budget_bytes) s << " budget=" << (budget_bytes / kMiB) << "MiB";
        s << (unified_memory ? " UMA" : "");
        s << " fp16=" << (shader_float16 ? "yes" : "no") << " fp64=" << (shader_float64 ? "yes" : "no");
        s << " subgroup=" << subgroup_size;
        s << (async_compute_queue ? " async-compute" : " single-queue");
    } else {
        s << " threads=" << cpu_threads;
    }
    return s.str();
}

DeviceCaps cpu_device_caps() {
    DeviceCaps c;
    c.name = "CPU reference";
    c.backend = BackendKind::Cpu;
    c.cpu_threads = std::thread::hardware_concurrency();
    c.unified_memory = true;
    c.shader_float16 = false;  // fp16 is emulated with fp32 arithmetic on the CPU
    c.shader_float64 = true;
    return c;
}

DeviceCaps intel_hd_graphics_gen11_caps() {
    DeviceCaps c;
    c.name = "Intel(R) HD Graphics Gen11";
    c.backend = BackendKind::Vulkan;
    c.vendor_id = 0x8086;
    c.device_id = 0x4E55;
    c.api_version = 4206807u;  // 1.3.215
    c.integrated = true;
    c.unified_memory = true;                 // memory types 1/7/15 all on heap 0
    c.device_local_bytes = 0xF91A8800ull;    // 3.89 GiB shared with the system
    c.max_allocation_bytes = 4179265536ull;
    c.max_workgroup_invocations = 1024;
    c.max_shared_memory_bytes = 32768;
    c.subgroup_size = 32;
    c.max_storage_buffer_bytes = 1073741820ull;
    c.shader_float16 = true;
    c.shader_float64 = false;
    c.timeline_semaphore = true;
    c.async_compute_queue = false;           // one family: graphics|compute|transfer
    c.timestamp_period_ns = 52.083332f;
    return c;
}

}  // namespace isc
