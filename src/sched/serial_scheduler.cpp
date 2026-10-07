#include <exception>
#include <stdexcept>

#include "isc/sched/scheduler.hpp"

namespace isc::sched {
namespace {

// Deterministic single-threaded fallback and the reference for scheduler
// tests: same graph semantics as the enkiTS backend, zero concurrency.
class SerialScheduler final : public Scheduler {
public:
    std::string_view name() const override { return "serial"; }
    std::uint32_t thread_count() const override { return 1; }

    void parallel_for(std::uint32_t count, std::uint32_t /*min_range*/, const RangeFn& fn) override {
        if (count > 0) fn(0, count, 0);
    }

    void run(const TaskGraph& graph) override {
        std::vector<TaskId> order;
        if (!graph.topological_order(order)) throw std::runtime_error("task graph contains a cycle");
        std::exception_ptr first;
        for (TaskId id : order) {
            if (first) break;  // stop scheduling new work after a failure
            try {
                graph.run_task(id);
            } catch (...) {
                first = std::current_exception();
            }
        }
        if (first) std::rethrow_exception(first);
    }
};

}  // namespace

std::unique_ptr<Scheduler> make_serial_scheduler() { return std::make_unique<SerialScheduler>(); }

std::unique_ptr<Scheduler> make_default_scheduler(std::uint32_t threads) {
    if (auto s = make_enkits_scheduler(threads)) return s;
    return make_serial_scheduler();
}

}  // namespace isc::sched
