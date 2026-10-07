#include "isc/runtime.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#include "isc/core/frame.hpp"
#include "isc/plan/execution_plan.hpp"

namespace isc {

struct Runtime::Impl {
    RuntimeOptions options;
    std::unique_ptr<sched::Scheduler> scheduler;
    std::unique_ptr<Backend> backend;
    BackendKind backend_kind = BackendKind::Auto;
    ir::Module module;
    RenderProfile profile;
    std::vector<std::string> notes;
    lod::AdaptiveController controller;
    bool loaded = false;
    FrameIndex frame = 0;
    FrameStage stage = FrameStage::Idle;

    bool select_backend(BackendKind wanted, std::ostringstream& report) {
        auto make_cpu = [&] {
            if (backend_kind != BackendKind::Cpu || !backend) {
                backend = make_cpu_backend(*scheduler);
                backend_kind = BackendKind::Cpu;
            }
        };
        auto make_vk = [&](std::string& err) -> bool {
            if (backend_kind == BackendKind::Vulkan && backend) return true;
            VulkanBackendOptions vo;
            vo.enable_validation = options.vulkan_validation;
            vo.device_index = options.vulkan_device;
            auto b = make_vulkan_backend(vo, &err);
            if (!b) return false;
            backend = std::move(b);
            backend_kind = BackendKind::Vulkan;
            return true;
        };

        std::string err;
        switch (wanted) {
            case BackendKind::Cpu: make_cpu(); break;
            case BackendKind::Vulkan:
                if (!make_vk(err)) {
                    report << "error: Vulkan backend requested but unavailable: " << err << '\n';
                    return false;
                }
                break;
            case BackendKind::Auto:
                if (!make_vk(err)) {
                    report << "note: Vulkan unavailable (" << err << "), using the CPU reference backend\n";
                    make_cpu();
                }
                break;
        }
        report << "backend: " << backend->caps().summary() << '\n';
        return true;
    }

    // Downgrade precisions the backend cannot execute, with a warning.
    void apply_precision_support(std::ostringstream& report) {
        auto fix = [&](Precision& p, const char* what) {
            if (backend_kind == BackendKind::Vulkan && p == Precision::FP16) {
                report << "warning: " << what << " fp16 kernels are not implemented in the Vulkan backend yet; using fp32\n";
                p = Precision::FP32;
            } else if (!backend->caps().supports(p)) {
                report << "warning: " << what << ' ' << to_string(p) << " is not supported by " << backend->name()
                       << "; using fp32" << (backend_kind == BackendKind::Cpu ? " (emulated)" : "") << '\n';
                p = Precision::FP32;
            }
        };
        fix(module.numerics.precision, "precision");
        fix(module.numerics.accumulation, "accumulation");
    }
};

Runtime::Runtime(RuntimeOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
    impl_->scheduler = sched::make_default_scheduler(impl_->options.threads);
}

Runtime::~Runtime() {
    if (impl_ && impl_->backend) impl_->backend->wait_idle();
}

bool Runtime::load(std::string_view source, std::string_view file_name, std::string* report_out) {
    Impl& s = *impl_;
    std::ostringstream report;
    s.loaded = false;

    ir::CompileResult compiled = ir::compile(source);
    report << compiled.diagnostics.format(file_name);
    if (!compiled.ok()) {
        if (report_out) *report_out = report.str();
        return false;
    }
    s.module = std::move(compiled.module);

    if (!s.options.profile_override.empty()) {
        RenderProfile p;
        if (!builtin_profile(s.options.profile_override, p)) {
            report << "error: unknown profile override '" << s.options.profile_override << "'\n";
            if (report_out) *report_out = report.str();
            return false;
        }
        s.module.profile = p;
    }

    const BackendKind wanted = s.options.backend != BackendKind::Auto ? s.options.backend : s.module.render.backend;
    if (!s.select_backend(wanted, report)) {
        if (report_out) *report_out = report.str();
        return false;
    }
    report << "scheduler: " << s.scheduler->name() << " (" << s.scheduler->thread_count() << " threads)\n";
    s.apply_precision_support(report);

    ProfileTuning tuned = tune_profile_for_device(s.module.profile, s.backend->caps());
    s.profile = tuned.profile;
    s.notes = tuned.notes;
    for (const auto& n : s.notes) report << "tuning: " << n << '\n';

    s.backend->configure(s.profile);
    s.controller.reset(s.profile);
    s.frame = 0;
    s.loaded = true;
    if (report_out) *report_out = report.str();
    return true;
}

bool Runtime::load_file(const std::string& path, std::string* report) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (report) *report = "error: cannot open '" + path + "'\n";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return load(ss.str(), path, report);
}

bool Runtime::process(const RenderTarget& input, RenderTarget& output, double host_frame_ms, FrameStats* stats_out,
                      std::string* error) {
    Impl& s = *impl_;
    if (!s.loaded) {
        if (error) *error = "no pipeline loaded";
        return false;
    }
    if (s.stage != FrameStage::Idle) {
        if (error) *error = "process() is not re-entrant";
        return false;
    }
    if (input.empty()) {
        if (error) *error = "input frame is empty";
        return false;
    }

    s.stage = FrameStage::Captured;
    const std::uint32_t W = input.width(), H = input.height();

    // Memory-aware dynamic resolution: never plan more than the budget allows.
    if (s.profile.dynamic_resolution) {
        const float cap = plan::max_scale_for_budget(s.module, s.controller.lod(), W, H, s.profile.memory_budget_bytes(),
                                                     s.profile.resolution_scale_min, s.profile.resolution_scale_max,
                                                     s.profile.resolution_scale_step);
        s.controller.set_scale_cap(cap);
    }
    const plan::ExecutionPlan frame_plan =
        plan::make_plan(s.module, s.controller.lod(), s.controller.resolution_scale(), W, H);

    FrameStats stats;
    s.stage = FrameStage::Processing;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = s.backend->execute(frame_plan, input, output, stats, error);
    const auto t1 = std::chrono::steady_clock::now();
    s.stage = FrameStage::Composited;

    stats.frame = ++s.frame;
    stats.layer_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    stats.rung_changed = s.controller.update(stats.layer_ms, host_frame_ms);
    stats.load = s.controller.last_load();
    s.stage = FrameStage::Presented;
    if (stats_out) *stats_out = std::move(stats);
    s.stage = FrameStage::Idle;
    return ok;
}

bool Runtime::loaded() const { return impl_->loaded; }
const ir::Module& Runtime::module() const { return impl_->module; }
const RenderProfile& Runtime::profile() const { return impl_->profile; }
const std::vector<std::string>& Runtime::tuning_notes() const { return impl_->notes; }
Backend* Runtime::backend() { return impl_->backend.get(); }
sched::Scheduler& Runtime::scheduler() { return *impl_->scheduler; }
lod::AdaptiveController& Runtime::controller() { return impl_->controller; }
FrameIndex Runtime::frame_index() const { return impl_->frame; }

}  // namespace isc
