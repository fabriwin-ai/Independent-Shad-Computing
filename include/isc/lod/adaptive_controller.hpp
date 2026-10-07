#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "isc/core/profile.hpp"
#include "isc/core/types.hpp"

namespace isc::lod {

// Adaptive cost controller combining LOD and dynamic resolution into a single
// ordered "quality ladder" (cheapest rung first):
//
//   [LOD0] , [LOD1 @ s_min .. s_max] , [LOD2 @ s_min .. s_max] , [LOD3 @ ...]
//
// Over budget  -> step down one rung after `downscale_frames` frames
//                (resolution drops first, then LOD).
// Under budget -> step up one rung after `hysteresis_frames` consecutive
//                frames below `upscale_threshold` x budget.
// In between   -> dead band, counters reset (prevents oscillation).
class AdaptiveController {
public:
    struct Rung {
        Lod lod;
        float scale;
    };

    explicit AdaptiveController(const RenderProfile& profile = RenderProfile{});
    void reset(const RenderProfile& profile);

    // layer_ms: time the layer spent this frame. host_frame_ms: full host
    // frame time (0 = unknown). Returns true when the rung changed.
    bool update(double layer_ms, double host_frame_ms = 0.0);

    // Memory-driven cap on the resolution scale (from max_scale_for_budget).
    // Steps down immediately if the current rung exceeds the cap.
    void set_scale_cap(float max_scale);

    // Jump to a rung (debugging / tests); adaptation continues from there on
    // the next update() unless the controller is pinned. Returns false if no
    // rung matches.
    bool force(Lod lod, float scale);

    // While pinned, update() still measures load but never changes the rung
    // (benchmarking a fixed rung, e.g. `isc_run --lod 1 --scale 0.75`). The
    // memory cap (set_scale_cap) is a hard limit and still applies.
    void set_pinned(bool pinned) {
        pinned_ = pinned;
        over_ = under_ = 0;
    }
    bool pinned() const { return pinned_; }

    Lod lod() const { return ladder_[rung_].lod; }
    float resolution_scale() const { return ladder_[rung_].scale; }
    std::size_t rung() const { return rung_; }
    std::size_t rung_count() const { return ladder_.size(); }
    const std::vector<Rung>& ladder() const { return ladder_; }
    double last_load() const { return last_load_; }

private:
    bool rung_allowed(std::size_t i) const;

    RenderProfile profile_;
    std::vector<Rung> ladder_;
    std::size_t rung_ = 0;
    std::uint32_t over_ = 0;
    std::uint32_t under_ = 0;
    float scale_cap_ = 1.0f;
    double last_load_ = 0.0;
    bool pinned_ = false;
};

}  // namespace isc::lod
