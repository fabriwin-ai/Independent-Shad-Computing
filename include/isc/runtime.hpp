#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "isc/backend/backend.hpp"
#include "isc/core/profile.hpp"
#include "isc/core/render_target.hpp"
#include "isc/ir/ir.hpp"
#include "isc/lod/adaptive_controller.hpp"
#include "isc/sched/scheduler.hpp"

namespace isc {

struct RuntimeOptions {
    BackendKind backend = BackendKind::Auto;  // overrides `render { backend = ...; }` unless Auto
    std::uint32_t threads = 0;                // scheduler threads, 0 = hardware concurrency
    bool vulkan_validation = false;
    int vulkan_device = -1;
    std::string profile_override;             // built-in profile name, empty = use the .fa source
};

// Minimal host-facing API. The host engine stays the host: it hands a frame
// in, gets an enhanced frame out, and never exposes its renderer internals.
//
//   isc::Runtime rt;
//   rt.load_file("pipeline.fa", &report);
//   each frame: rt.process(host_frame, enhanced, host_frame_ms);
class Runtime {
public:
    explicit Runtime(RuntimeOptions options = {});
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Compile, pick + configure the backend, tune the profile for the device.
    // `report` receives diagnostics, backend selection and tuning notes.
    bool load(std::string_view source, std::string_view file_name = "<source>", std::string* report = nullptr);
    bool load_file(const std::string& path, std::string* report = nullptr);

    // One frame: capture -> enhance -> composite -> output. Not re-entrant.
    bool process(const RenderTarget& input, RenderTarget& output, double host_frame_ms = 0.0,
                 FrameStats* stats = nullptr, std::string* error = nullptr);

    bool loaded() const;
    const ir::Module& module() const;
    const RenderProfile& profile() const;  // device-tuned
    const std::vector<std::string>& tuning_notes() const;
    Backend* backend();
    sched::Scheduler& scheduler();
    lod::AdaptiveController& controller();
    FrameIndex frame_index() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace isc
