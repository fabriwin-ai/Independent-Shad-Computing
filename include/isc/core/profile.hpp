#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "isc/core/device_caps.hpp"
#include "isc/core/types.hpp"

namespace isc {

// Render-enhance profile: runtime settings for cost control, dynamic
// resolution, memory allocation and resource caching. Declared in .fa source
// with a `profile <name> { ... }` block or picked from the built-ins
// (performance / balanced / quality). See docs/PROFILES.md.
struct RenderProfile {
    std::string name = "balanced";

    // --- frame budget ----------------------------------------------------
    double target_fps = 60.0;          // host frame budget = 1000 / target_fps
    double enhance_budget_ms = 4.0;    // time the layer itself may spend per frame

    // --- adaptive LOD ------------------------------------------------------
    bool adaptive_lod = true;
    Lod lod_min = Lod::Bypass;
    Lod lod_max = Lod::Iterative;
    Lod lod_start = Lod::Extended;

    // --- dynamic resolution (internal working scale of the enhancement) ----
    bool dynamic_resolution = true;
    float resolution_scale_min = 0.5f;
    float resolution_scale_max = 1.0f;
    float resolution_scale_step = 0.125f;
    float resolution_scale_start = 1.0f;

    // --- controller tuning (hysteresis) -------------------------------------
    double upscale_threshold = 0.75;   // step up when load < threshold ...
    std::uint32_t hysteresis_frames = 30;  // ... for this many consecutive frames
    std::uint32_t downscale_frames = 2;    // step down after N over-budget frames

    // --- memory ------------------------------------------------------------
    std::uint64_t memory_budget_mb = 256;  // hard cap for layer allocations
    std::uint64_t memory_block_mb = 32;    // device memory block size (sub-allocated)
    std::uint64_t buffer_cache_mb = 64;    // recycled buffers kept alive across frames
    std::uint32_t frames_in_flight = 2;

    double frame_budget_ms() const { return target_fps > 0.0 ? 1000.0 / target_fps : 0.0; }
    std::uint64_t memory_budget_bytes() const { return memory_budget_mb * kMiB; }
    std::uint64_t memory_block_bytes() const { return memory_block_mb * kMiB; }
    std::uint64_t buffer_cache_bytes() const { return buffer_cache_mb * kMiB; }
};

bool builtin_profile(std::string_view name, RenderProfile& out);
std::vector<std::string_view> builtin_profile_names();

// Returns an empty string when valid, otherwise a human-readable reason.
std::string validate_profile(const RenderProfile& p);

struct ProfileTuning {
    RenderProfile profile;
    std::vector<std::string> notes;  // what was changed and why
};

// Clamps a profile to what the device can sustain: memory budget vs heap size
// (stricter on shared/UMA memory), block size vs maxMemoryAllocationSize,
// cache vs budget, frames in flight vs queue topology.
ProfileTuning tune_profile_for_device(const RenderProfile& p, const DeviceCaps& caps);

std::string describe(const RenderProfile& p);

}  // namespace isc
