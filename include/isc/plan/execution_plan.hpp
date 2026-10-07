#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "isc/core/types.hpp"
#include "isc/ir/ir.hpp"

namespace isc::plan {

// A frame-specific, backend-neutral instantiation of the IR: LOD and dynamic
// resolution are resolved into concrete per-stage work.
struct StagePlan {
    std::uint32_t artifact = 0;
    std::string name;
    std::int32_t source_stage = -1;  // -1 = captured framebuffer, else index into stages
    bool gradient = false;           // run reduction + optimisation this frame
    std::uint32_t iterations = 0;
    float weight = 0.25f;
    float learning_rate = 0.25f;
    float target = 1.25f;
    float weight_min = 0.0f;
    float weight_max = 4.0f;
    std::uint32_t radius = 1;
};

struct ExecutionPlan {
    Lod lod = Lod::Bypass;
    float resolution_scale = 1.0f;
    Precision precision = Precision::FP32;
    Precision accumulation = Precision::FP32;
    std::uint32_t width = 0, height = 0;            // host framebuffer
    std::uint32_t work_width = 0, work_height = 0;  // enhancement working resolution
    std::vector<StagePlan> stages;                  // topological order

    bool bypass() const { return lod == Lod::Bypass || stages.empty(); }
    bool scaled() const { return work_width != width || work_height != height; }
};

// LOD mapping (LOD.md):
//   LOD0 bypass   -> no stages
//   LOD1 basic    -> artifact passes with the declared weight, no gradient
//   LOD2 extended -> + one gradient step (if the artifact enables gradient)
//   LOD3 iterative-> + all declared iterations
ExecutionPlan make_plan(const ir::Module& module, Lod lod, float resolution_scale, std::uint32_t width,
                        std::uint32_t height);

std::uint32_t scaled_extent(std::uint32_t full, float scale);

// Upper bound of transient memory for one frame (RGBA32F buffers):
// capture + per-stage full-res output + per-stage working buffers.
std::uint64_t estimate_frame_bytes(const ExecutionPlan& plan);

// Largest scale on the [min, max] ladder whose frame estimate fits `budget`.
// Returns `min` if nothing fits (the caller should then drop the LOD).
float max_scale_for_budget(const ir::Module& module, Lod lod, std::uint32_t width, std::uint32_t height,
                           std::uint64_t budget_bytes, float scale_min, float scale_max, float step);

std::string describe(const ExecutionPlan& plan);

}  // namespace isc::plan
