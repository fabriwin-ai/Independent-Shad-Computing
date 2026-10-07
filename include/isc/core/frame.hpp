#pragma once

#include "isc/core/render_target.hpp"
#include "isc/core/types.hpp"

namespace isc {

// Frame lifecycle inside the layer:
//   Idle -> Captured -> Processing -> Composited -> Presented -> Idle
// The host owns the frame before Captured and after Presented.
enum class FrameStage : std::uint8_t { Idle, Captured, Processing, Composited, Presented };

struct Frame {
    FrameIndex index = 0;
    FrameStage stage = FrameStage::Idle;
    const RenderTarget* input = nullptr;  // host framebuffer (read-only for the layer)
    RenderTarget* output = nullptr;       // host-provided destination
    double host_frame_ms = 0.0;           // last full host frame time, 0 if unknown
};

inline const char* to_string(FrameStage s) {
    switch (s) {
        case FrameStage::Idle: return "idle";
        case FrameStage::Captured: return "captured";
        case FrameStage::Processing: return "processing";
        case FrameStage::Composited: return "composited";
        case FrameStage::Presented: return "presented";
    }
    return "?";
}

}  // namespace isc
