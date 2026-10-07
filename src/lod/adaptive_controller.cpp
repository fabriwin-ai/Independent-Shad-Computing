#include "isc/lod/adaptive_controller.hpp"

#include <algorithm>
#include <cmath>

namespace isc::lod {
namespace {

std::vector<float> scale_steps(const RenderProfile& p) {
    std::vector<float> s;
    if (!p.dynamic_resolution) {
        s.push_back(p.resolution_scale_start);
        return s;
    }
    const float step = p.resolution_scale_step > 0.0f ? p.resolution_scale_step : 0.125f;
    for (float v = p.resolution_scale_min; v < p.resolution_scale_max - 1e-4f; v += step) s.push_back(v);
    s.push_back(p.resolution_scale_max);
    return s;
}

}  // namespace

AdaptiveController::AdaptiveController(const RenderProfile& profile) { reset(profile); }

void AdaptiveController::reset(const RenderProfile& p) {
    profile_ = p;
    ladder_.clear();
    over_ = under_ = 0;
    scale_cap_ = 1.0f;
    last_load_ = 0.0;

    const std::vector<float> scales = scale_steps(p);
    int lo = static_cast<int>(p.adaptive_lod ? p.lod_min : p.lod_start);
    int hi = static_cast<int>(p.adaptive_lod ? p.lod_max : p.lod_start);
    if (lo == 0) {
        ladder_.push_back({Lod::Bypass, scales.front()});
        lo = 1;
    }
    for (int l = lo; l <= hi; ++l)
        for (float s : scales) ladder_.push_back({static_cast<Lod>(l), s});
    if (ladder_.empty()) ladder_.push_back({Lod::Bypass, 1.0f});

    // Start on the rung closest to (lod_start, resolution_scale_start).
    rung_ = 0;
    float best = 1e9f;
    for (std::size_t i = 0; i < ladder_.size(); ++i) {
        const float d = std::fabs(static_cast<float>(static_cast<int>(ladder_[i].lod) - static_cast<int>(p.lod_start))) * 10.0f +
                        std::fabs(ladder_[i].scale - p.resolution_scale_start);
        if (d < best) {
            best = d;
            rung_ = i;
        }
    }
}

bool AdaptiveController::rung_allowed(std::size_t i) const {
    return ladder_[i].lod == Lod::Bypass || ladder_[i].scale <= scale_cap_ + 1e-4f;
}

bool AdaptiveController::update(double layer_ms, double host_frame_ms) {
    const double layer_load = profile_.enhance_budget_ms > 0.0 ? layer_ms / profile_.enhance_budget_ms : 0.0;
    const double frame_budget = profile_.frame_budget_ms();
    const double host_load = (host_frame_ms > 0.0 && frame_budget > 0.0) ? host_frame_ms / frame_budget : 0.0;
    last_load_ = std::max(layer_load, host_load);

    const std::size_t before = rung_;
    if (last_load_ > 1.0) {
        under_ = 0;
        if (++over_ >= profile_.downscale_frames && rung_ > 0) {
            std::size_t j = rung_ - 1;
            while (j > 0 && !rung_allowed(j)) --j;  // skip rungs above the memory cap
            rung_ = j;
            over_ = 0;
        }
    } else if (last_load_ < profile_.upscale_threshold) {
        over_ = 0;
        if (++under_ >= profile_.hysteresis_frames) {
            std::size_t j = rung_ + 1;
            while (j < ladder_.size() && !rung_allowed(j)) ++j;
            if (j < ladder_.size()) rung_ = j;
            under_ = 0;
        }
    } else {
        over_ = under_ = 0;  // dead band
    }
    return rung_ != before;
}

void AdaptiveController::set_scale_cap(float max_scale) {
    scale_cap_ = max_scale;
    while (rung_ > 0 && !rung_allowed(rung_)) --rung_;
}

bool AdaptiveController::force(Lod lod, float scale) {
    for (std::size_t i = 0; i < ladder_.size(); ++i) {
        if (ladder_[i].lod == lod && (lod == Lod::Bypass || std::fabs(ladder_[i].scale - scale) < 1e-3f)) {
            rung_ = i;
            over_ = under_ = 0;
            return true;
        }
    }
    return false;
}

}  // namespace isc::lod
