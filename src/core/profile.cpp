#include "isc/core/profile.hpp"

#include <algorithm>
#include <sstream>

namespace isc {
namespace {

RenderProfile make_performance() {
    RenderProfile p;
    p.name = "performance";
    p.enhance_budget_ms = 2.0;
    p.lod_min = Lod::Bypass;
    p.lod_max = Lod::Extended;
    p.lod_start = Lod::Basic;
    p.resolution_scale_min = 0.5f;
    p.resolution_scale_max = 0.75f;
    p.resolution_scale_start = 0.75f;
    p.hysteresis_frames = 20;
    p.memory_budget_mb = 128;
    p.memory_block_mb = 16;
    p.buffer_cache_mb = 32;
    return p;
}

RenderProfile make_balanced() {
    RenderProfile p;  // defaults are the balanced profile
    p.name = "balanced";
    return p;
}

RenderProfile make_quality() {
    RenderProfile p;
    p.name = "quality";
    p.enhance_budget_ms = 8.0;
    p.lod_min = Lod::Basic;
    p.lod_max = Lod::Iterative;
    p.lod_start = Lod::Iterative;
    p.resolution_scale_min = 0.75f;
    p.resolution_scale_max = 1.0f;
    p.resolution_scale_start = 1.0f;
    p.upscale_threshold = 0.8;
    p.hysteresis_frames = 60;
    p.memory_budget_mb = 512;
    p.memory_block_mb = 64;
    p.buffer_cache_mb = 128;
    return p;
}

}  // namespace

bool builtin_profile(std::string_view name, RenderProfile& out) {
    if (name == "performance") { out = make_performance(); return true; }
    if (name == "balanced") { out = make_balanced(); return true; }
    if (name == "quality") { out = make_quality(); return true; }
    return false;
}

std::vector<std::string_view> builtin_profile_names() { return {"performance", "balanced", "quality"}; }

std::string validate_profile(const RenderProfile& p) {
    if (!(p.target_fps > 0.0)) return "target_fps must be > 0";
    if (!(p.enhance_budget_ms > 0.0)) return "enhance_budget_ms must be > 0";
    if (p.lod_min > p.lod_max) return "lod_min must be <= lod_max";
    if (p.lod_start < p.lod_min || p.lod_start > p.lod_max) return "lod_start must lie in [lod_min, lod_max]";
    if (!(p.resolution_scale_min > 0.0f)) return "resolution_scale_min must be > 0";
    if (p.resolution_scale_max > 1.0f) return "resolution_scale_max must be <= 1.0";
    if (p.resolution_scale_min > p.resolution_scale_max) return "resolution_scale_min must be <= resolution_scale_max";
    if (p.resolution_scale_start < p.resolution_scale_min || p.resolution_scale_start > p.resolution_scale_max)
        return "resolution_scale_start must lie in [resolution_scale_min, resolution_scale_max]";
    if (!(p.resolution_scale_step > 0.0f)) return "resolution_scale_step must be > 0";
    if (!(p.upscale_threshold > 0.0 && p.upscale_threshold < 1.0)) return "upscale_threshold must be in (0, 1)";
    if (p.hysteresis_frames == 0) return "hysteresis_frames must be >= 1";
    if (p.downscale_frames == 0) return "downscale_frames must be >= 1";
    if (p.memory_budget_mb < 16) return "memory_budget_mb must be >= 16";
    if (p.memory_block_mb == 0 || p.memory_block_mb > p.memory_budget_mb)
        return "memory_block_mb must be in [1, memory_budget_mb]";
    if (p.buffer_cache_mb > p.memory_budget_mb) return "buffer_cache_mb must be <= memory_budget_mb";
    if (p.frames_in_flight < 1 || p.frames_in_flight > 4) return "frames_in_flight must be in [1, 4]";
    return {};
}

ProfileTuning tune_profile_for_device(const RenderProfile& in, const DeviceCaps& caps) {
    ProfileTuning t{in, {}};
    RenderProfile& p = t.profile;
    auto note = [&](const std::string& s) { t.notes.push_back(s); };

    if (caps.device_local_bytes > 0) {
        // On UMA/integrated parts the "VRAM" is system RAM shared with the game,
        // so the layer is allowed a much smaller slice.
        const bool shared = caps.unified_memory && caps.integrated;
        const double fraction = shared ? 0.10 : 0.25;
        std::uint64_t cap = static_cast<std::uint64_t>(static_cast<double>(caps.device_local_bytes) * fraction);
        if (caps.budget_bytes > 0) cap = std::min(cap, caps.budget_bytes / 2);
        const std::uint64_t cap_mb = std::max<std::uint64_t>(16, cap / kMiB);
        if (p.memory_budget_mb > cap_mb) {
            std::ostringstream s;
            s << "memory_budget_mb " << p.memory_budget_mb << " -> " << cap_mb << " ("
              << (shared ? "10% of shared UMA heap" : "25% of device-local heap")
              << (caps.budget_bytes ? ", <= 50% of VK_EXT_memory_budget" : "") << ")";
            note(s.str());
            p.memory_budget_mb = cap_mb;
        }
    }

    if (caps.max_allocation_bytes > 0) {
        const std::uint64_t max_mb = std::max<std::uint64_t>(1, caps.max_allocation_bytes / kMiB);
        if (p.memory_block_mb > max_mb) {
            note("memory_block_mb clamped to maxMemoryAllocationSize (" + std::to_string(max_mb) + " MiB)");
            p.memory_block_mb = max_mb;
        }
    }
    if (p.memory_block_mb > p.memory_budget_mb) {
        note("memory_block_mb " + std::to_string(p.memory_block_mb) + " -> " + std::to_string(p.memory_budget_mb / 4) +
             " (must fit the memory budget)");
        p.memory_block_mb = std::max<std::uint64_t>(1, p.memory_budget_mb / 4);
    }

    const std::uint64_t cache_cap = p.memory_budget_mb / 2;
    if (p.buffer_cache_mb > cache_cap) {
        note("buffer_cache_mb " + std::to_string(p.buffer_cache_mb) + " -> " + std::to_string(cache_cap) +
             " (cache may use at most half of the memory budget)");
        p.buffer_cache_mb = cache_cap;
    }

    if (caps.backend == BackendKind::Vulkan && !caps.async_compute_queue && p.frames_in_flight > 2) {
        note("frames_in_flight " + std::to_string(p.frames_in_flight) +
             " -> 2 (single graphics/compute queue: deeper pipelining only adds latency)");
        p.frames_in_flight = 2;
    }
    if (caps.backend == BackendKind::Cpu && p.frames_in_flight != 1) {
        note("frames_in_flight -> 1 (CPU reference backend executes synchronously)");
        p.frames_in_flight = 1;
    }
    return t;
}

std::string describe(const RenderProfile& p) {
    std::ostringstream s;
    s << "profile " << p.name << ":\n"
      << "  budget       : " << p.target_fps << " fps (" << p.frame_budget_ms() << " ms), layer "
      << p.enhance_budget_ms << " ms\n"
      << "  lod          : " << (p.adaptive_lod ? "adaptive " : "fixed ") << to_string(p.lod_min) << " .. "
      << to_string(p.lod_max) << " start " << to_string(p.lod_start) << "\n"
      << "  resolution   : " << (p.dynamic_resolution ? "dynamic " : "fixed ") << p.resolution_scale_min << " .. "
      << p.resolution_scale_max << " step " << p.resolution_scale_step << " start " << p.resolution_scale_start
      << "\n"
      << "  hysteresis   : up < " << p.upscale_threshold << " for " << p.hysteresis_frames << " frames, down after "
      << p.downscale_frames << " frames\n"
      << "  memory       : budget " << p.memory_budget_mb << " MiB, block " << p.memory_block_mb << " MiB, cache "
      << p.buffer_cache_mb << " MiB, frames in flight " << p.frames_in_flight << "\n";
    return s.str();
}

}  // namespace isc
