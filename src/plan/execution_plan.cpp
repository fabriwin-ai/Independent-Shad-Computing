#include "isc/plan/execution_plan.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "isc/core/render_target.hpp"

namespace isc::plan {

std::uint32_t scaled_extent(std::uint32_t full, float scale) {
    if (full == 0) return 0;
    const auto v = static_cast<std::uint32_t>(std::lround(static_cast<double>(full) * scale));
    return std::clamp<std::uint32_t>(v, 1u, full);
}

ExecutionPlan make_plan(const ir::Module& m, Lod lod, float scale, std::uint32_t width, std::uint32_t height) {
    ExecutionPlan p;
    p.lod = lod;
    p.resolution_scale = std::clamp(scale, 0.05f, 1.0f);
    p.precision = m.numerics.precision;
    p.accumulation = m.numerics.accumulation;
    p.width = width;
    p.height = height;
    p.work_width = scaled_extent(width, p.resolution_scale);
    p.work_height = scaled_extent(height, p.resolution_scale);
    if (lod == Lod::Bypass) return p;

    std::vector<std::int32_t> stage_of(m.artifacts.size(), -1);
    for (std::uint32_t idx : m.order) {
        const ir::Artifact& a = m.artifacts[idx];
        StagePlan s;
        s.artifact = idx;
        s.name = a.name;
        s.source_stage = a.source == ir::kFramebuffer ? -1 : stage_of[a.source];
        s.weight = a.weight;
        s.learning_rate = a.learning_rate;
        s.target = a.target;
        s.weight_min = a.weight_min;
        s.weight_max = a.weight_max;
        s.radius = a.radius;
        if (a.gradient && a.iterations > 0) {
            if (lod == Lod::Extended) {
                s.gradient = true;
                s.iterations = 1;
            } else if (lod == Lod::Iterative) {
                s.gradient = true;
                s.iterations = a.iterations;
            }
        }
        stage_of[idx] = static_cast<std::int32_t>(p.stages.size());
        p.stages.push_back(std::move(s));
    }
    return p;
}

std::uint64_t estimate_frame_bytes(const ExecutionPlan& p) {
    const std::uint64_t full = RenderTarget::bytes_for(p.width, p.height);
    if (p.bypass()) return full;  // capture only
    const std::uint64_t work = RenderTarget::bytes_for(p.work_width, p.work_height);
    const std::uint64_t per_stage = full                    // stage output
                                    + 2 * work              // blur scratch + detail
                                    + (p.scaled() ? work : 0);  // downsampled input
    return full + per_stage * p.stages.size();
}

float max_scale_for_budget(const ir::Module& m, Lod lod, std::uint32_t width, std::uint32_t height,
                           std::uint64_t budget, float smin, float smax, float step) {
    if (step <= 0.0f) step = 0.125f;
    for (float s = smax; s >= smin - 1e-6f; s -= step) {
        if (estimate_frame_bytes(make_plan(m, lod, s, width, height)) <= budget) return s;
    }
    return smin;
}

std::string describe(const ExecutionPlan& p) {
    std::ostringstream s;
    s << to_string(p.lod) << " scale=" << p.resolution_scale << " full=" << p.width << 'x' << p.height
      << " work=" << p.work_width << 'x' << p.work_height << " precision=" << to_string(p.precision) << '/'
      << to_string(p.accumulation) << " est=" << (estimate_frame_bytes(p) / 1024) << "KiB\n";
    for (std::size_t i = 0; i < p.stages.size(); ++i) {
        const StagePlan& st = p.stages[i];
        s << "  stage " << i << ' ' << st.name << " <- "
          << (st.source_stage < 0 ? std::string("framebuffer") : p.stages[st.source_stage].name)
          << " gradient=" << (st.gradient ? "on" : "off") << " iterations=" << st.iterations << " weight=" << st.weight
          << " radius=" << st.radius << '\n';
    }
    return s.str();
}

}  // namespace isc::plan
