#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include "backend/cpu/cpu_kernels.hpp"
#include "isc/backend/backend.hpp"
#include "isc/core/resource_cache.hpp"
#include "isc/opt/optimizer.hpp"
#include "isc/sched/scheduler.hpp"
#include "isc/sched/task_graph.hpp"

namespace isc {
namespace {

// Reference backend: builds a per-frame task graph that mirrors the README's
// parallel execution model and runs it on the scheduler (enkiTS or serial).
//
//   capture
//     -> [downsample]                      (dynamic resolution)
//     -> float-stats  ||  artifact-detail  (independent, run concurrently)
//     -> gradient                          (reduction + parameter optimisation)
//     -> composite
//   -> display
class CpuBackend final : public Backend {
public:
    explicit CpuBackend(sched::Scheduler& s) : sched_(s), cache_(64 * kMiB) {
        caps_ = cpu_device_caps();
        caps_.cpu_threads = s.thread_count();
        caps_.name = "CPU reference (" + std::string(s.name()) + ", " + std::to_string(s.thread_count()) + " threads)";
    }

    std::string_view name() const override { return "cpu"; }
    const DeviceCaps& caps() const override { return caps_; }

    void configure(const RenderProfile& p) override {
        budget_ = p.memory_budget_bytes();
        cache_.set_budget(p.buffer_cache_bytes());
    }

    bool execute(const plan::ExecutionPlan& plan, const RenderTarget& input, RenderTarget& output, FrameStats& stats,
                 std::string* error) override {
        if (input.empty()) {
            if (error) *error = "input render target is empty";
            return false;
        }
        if (output.width() != input.width() || output.height() != input.height())
            output.resize(input.width(), input.height());

        stats.lod = plan.lod;
        stats.resolution_scale = plan.resolution_scale;
        stats.work_width = plan.work_width;
        stats.work_height = plan.work_height;
        stats.stages.clear();
        frame_bytes_ = 0;

        if (plan.bypass()) {  // LOD0: framebuffer -> display
            std::memcpy(output.data(), input.data(), input.byte_size());
            output.set_ownership(Ownership::Host);
            fill_memory_stats(stats);
            return true;
        }

        const bool wide = plan.precision == Precision::FP64;
        const bool wide_acc = plan.accumulation == Precision::FP64;
        if (wide) {
            return wide_acc ? run<double, double>(plan, input, output, stats, error)
                            : run<double, float>(plan, input, output, stats, error);
        }
        return wide_acc ? run<float, double>(plan, input, output, stats, error)
                        : run<float, float>(plan, input, output, stats, error);
    }

private:
    struct StageData {
        RenderTarget work_in;  // downsampled source (dynamic resolution only)
        RenderTarget tmp;
        RenderTarget det;
        RenderTarget out;
        double A = 0.0;
        float weight = 0.0f;
        opt::OptimizerResult result;
    };

    RenderTarget acquire(std::uint32_t w, std::uint32_t h) {
        frame_bytes_ += RenderTarget::bytes_for(w, h);
        if (auto cached = cache_.take(CacheKey{w, h})) return std::move(*cached);
        return RenderTarget(w, h);
    }

    void release(RenderTarget& t) {
        if (t.empty()) return;
        const CacheKey key{t.width(), t.height()};
        const std::uint64_t bytes = t.byte_size();
        cache_.put(key, std::move(t), bytes);
        t = RenderTarget{};
    }

    void fill_memory_stats(FrameStats& stats) const {
        const CacheStats& c = cache_.stats();
        stats.memory.used_bytes = frame_bytes_;
        stats.memory.reserved_bytes = frame_bytes_ + c.bytes_cached;
        stats.memory.budget_bytes = budget_;
        stats.memory.blocks = 0;
        stats.memory.cache_bytes = c.bytes_cached;
        stats.memory.cache_hits = c.hits;
        stats.memory.cache_misses = c.misses;
        stats.memory.cache_evictions = c.evictions;
    }

    template <class T, class Acc>
    bool run(const plan::ExecutionPlan& plan, const RenderTarget& input, RenderTarget& output, FrameStats& stats,
             std::string* error) {
        const std::uint32_t W = plan.width, H = plan.height, w = plan.work_width, h = plan.work_height;
        const bool scaled = plan.scaled();
        const std::size_t n = plan.stages.size();

        // Allocation happens up front on the calling thread; tasks only compute.
        RenderTarget captured = acquire(W, H);
        std::vector<StageData> data(n);
        for (auto& d : data) {
            if (scaled) d.work_in = acquire(w, h);
            d.tmp = acquire(w, h);
            d.det = acquire(w, h);
            d.out = acquire(W, H);
        }

        sched::TaskGraph g;
        const sched::TaskId capture = g.add(
            "capture",
            [&] {
                std::memcpy(captured.data(), input.data(), input.byte_size());
                captured.set_ownership(Ownership::Cpu);
            },
            sched::TaskPriority::High);

        std::vector<sched::TaskId> composite_task(n);
        for (std::size_t i = 0; i < n; ++i) {
            const plan::StagePlan& sp = plan.stages[i];
            StageData& d = data[i];
            const RenderTarget* src = sp.source_stage < 0 ? &captured : &data[sp.source_stage].out;
            const sched::TaskId prev = sp.source_stage < 0 ? capture : composite_task[sp.source_stage];
            const RenderTarget* work = scaled ? &d.work_in : src;
            d.weight = sp.weight;

            sched::TaskId entry = prev;
            if (scaled) {
                entry = g.add("downsample:" + sp.name, [this, src, &d] { cpu::resample<T>(sched_, *src, d.work_in); });
                g.depend(entry, prev);
            }

            const sched::TaskId art = g.add("artifact:" + sp.name, [this, work, &d, &sp] {
                cpu::blur_h<T>(sched_, *work, d.tmp, sp.radius);
                cpu::detail<T>(sched_, *work, d.tmp, d.det, sp.radius);
            });
            g.depend(art, entry);

            sched::TaskId before_composite = art;
            if (sp.gradient) {
                const sched::TaskId fl =
                    g.add("float:" + sp.name, [this, work, &d] { d.A = cpu::edge_energy_a<Acc>(sched_, *work); });
                g.depend(fl, entry);

                const sched::TaskId grad = g.add("gradient:" + sp.name, [this, work, &d, &sp] {
                    opt::EdgeEnergy e;
                    e.A = d.A;
                    cpu::edge_energy_bc<Acc>(sched_, *work, d.det, e.B, e.C);
                    opt::EdgeEnergyLoss loss(e, sp.target);
                    opt::ParameterSet params;
                    params.add("weight", sp.weight, sp.weight_min, sp.weight_max);
                    opt::OptimizerSettings os;
                    os.learning_rate = sp.learning_rate;
                    os.max_iterations = sp.iterations;
                    d.result = opt::gradient_descent(loss, params, os);
                    d.weight = static_cast<float>(params[0].value);
                });
                g.depend(grad, fl);
                g.depend(grad, art);
                before_composite = grad;
            }

            composite_task[i] = g.add("composite:" + sp.name, [this, src, &d] {
                cpu::composite<T>(sched_, *src, d.det, d.weight, d.out);
            });
            g.depend(composite_task[i], before_composite);
        }

        RenderTarget* final_out = &data[n - 1].out;
        const sched::TaskId display = g.add(
            "display",
            [&output, final_out] {
                std::memcpy(output.data(), final_out->data(), final_out->byte_size());
                output.set_ownership(Ownership::Host);
            },
            sched::TaskPriority::High);
        g.depend(display, composite_task[n - 1]);

        bool ok = true;
        try {
            sched_.run(g);
        } catch (const std::exception& e) {
            if (error) *error = std::string("cpu backend: ") + e.what();
            ok = false;
        }

        for (std::size_t i = 0; i < n; ++i) {
            const plan::StagePlan& sp = plan.stages[i];
            StageStats st;
            st.name = sp.name;
            st.weight_in = sp.weight;
            st.weight_out = data[i].weight;
            st.iterations = data[i].result.iterations;
            st.loss_initial = data[i].result.initial_loss;
            st.loss_final = data[i].result.final_loss;
            stats.stages.push_back(std::move(st));
        }

        release(captured);
        for (auto& d : data) {
            release(d.work_in);
            release(d.tmp);
            release(d.det);
            release(d.out);
        }
        fill_memory_stats(stats);
        return ok;
    }

    sched::Scheduler& sched_;
    DeviceCaps caps_;
    ResourceCache<RenderTarget> cache_;
    std::uint64_t budget_ = 256 * kMiB;
    std::uint64_t frame_bytes_ = 0;
};

}  // namespace

std::unique_ptr<Backend> make_cpu_backend(sched::Scheduler& scheduler) {
    return std::make_unique<CpuBackend>(scheduler);
}

}  // namespace isc
