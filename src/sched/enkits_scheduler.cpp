#include "isc/sched/scheduler.hpp"

#if defined(ISC_HAS_ENKITS)

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "TaskScheduler.h"

namespace isc::sched {
namespace {

enki::TaskPriority to_enki(TaskPriority p) {
    // Works for any ENKITS_TASK_PRIORITIES_NUM: High -> 0, Low -> last, Normal -> middle.
    const int last = static_cast<int>(enki::TASK_PRIORITY_NUM) - 1;
    switch (p) {
        case TaskPriority::High: return static_cast<enki::TaskPriority>(0);
        case TaskPriority::Normal: return static_cast<enki::TaskPriority>(last / 2);
        case TaskPriority::Low: return static_cast<enki::TaskPriority>(last);
    }
    return static_cast<enki::TaskPriority>(0);
}

// Data-parallel loop body.
class RangeTask final : public enki::ITaskSet {
public:
    RangeTask(std::uint32_t count, std::uint32_t min_range, const RangeFn& fn) : fn_(fn) {
        m_SetSize = count;
        m_MinRange = min_range > 0 ? min_range : 1;
    }
    void ExecuteRange(enki::TaskSetPartition range, std::uint32_t thread) override {
        if (failed_.load(std::memory_order_relaxed)) return;
        try {
            fn_(range.start, range.end, thread);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) error_ = std::current_exception();
            failed_.store(true, std::memory_order_relaxed);
        }
    }
    void rethrow_if_failed() {
        if (error_) std::rethrow_exception(error_);
    }

private:
    const RangeFn& fn_;
    std::atomic<bool> failed_{false};
    std::mutex mutex_;
    std::exception_ptr error_;
};

struct GraphRun;

// One node of the frame task graph. Dependencies between nodes are expressed
// with enki::Dependency, so dependents are launched by enkiTS itself as soon
// as all their prerequisites complete - no frame-wide barriers.
class NodeTask final : public enki::ITaskSet {
public:
    GraphRun* run = nullptr;
    TaskId id = 0;
    void ExecuteRange(enki::TaskSetPartition, std::uint32_t) override;
};

struct GraphRun {
    const TaskGraph* graph = nullptr;
    std::atomic<std::uint32_t> completed{0};
    std::atomic<bool> failed{false};
    std::mutex mutex;
    std::exception_ptr error;
};

void NodeTask::ExecuteRange(enki::TaskSetPartition, std::uint32_t) {
    if (!run->failed.load(std::memory_order_acquire)) {
        try {
            run->graph->run_task(id);
        } catch (...) {
            std::lock_guard<std::mutex> lock(run->mutex);
            if (!run->error) run->error = std::current_exception();
            run->failed.store(true, std::memory_order_release);
        }
    }
    run->completed.fetch_add(1, std::memory_order_acq_rel);
}

class EnkiScheduler final : public Scheduler {
public:
    explicit EnkiScheduler(std::uint32_t threads) {
        const std::uint32_t hw = std::max(1u, std::thread::hardware_concurrency());
        ts_.Initialize(threads > 0 ? threads : hw);
    }
    ~EnkiScheduler() override { ts_.WaitforAllAndShutdown(); }

    std::string_view name() const override { return "enkiTS"; }
    std::uint32_t thread_count() const override { return ts_.GetNumTaskThreads(); }

    void parallel_for(std::uint32_t count, std::uint32_t min_range, const RangeFn& fn) override {
        if (count == 0) return;
        RangeTask task(count, min_range, fn);
        ts_.AddTaskSetToPipe(&task);
        ts_.WaitforTask(&task);
        task.rethrow_if_failed();
    }

    void run(const TaskGraph& graph) override {
        const std::size_t n = graph.size();
        if (n == 0) return;
        std::vector<TaskId> order;
        if (!graph.topological_order(order)) throw std::runtime_error("task graph contains a cycle");

        GraphRun state;
        state.graph = &graph;

        // Declaration order matters: dependencies are destroyed before the
        // tasks they reference.
        std::unique_ptr<NodeTask[]> tasks(new NodeTask[n]);
        std::deque<enki::Dependency> deps;

        for (TaskId i = 0; i < n; ++i) {
            tasks[i].run = &state;
            tasks[i].id = i;
            tasks[i].m_SetSize = 1;
            tasks[i].m_Priority = to_enki(graph.priority(i));
        }
        for (TaskId i = 0; i < n; ++i) {
            for (TaskId pre : graph.prerequisites(i)) {
                deps.emplace_back();
                deps.back().SetDependency(&tasks[pre], &tasks[i]);
            }
        }
        for (TaskId i = 0; i < n; ++i) {
            if (graph.prerequisites(i).empty()) ts_.AddTaskSetToPipe(&tasks[i]);
        }

        // Help execute until every node has run. Dependents are queued by
        // enkiTS before their prerequisite reports completion, so WaitforAll
        // drains the whole graph; the loop is a guard against early return.
        ts_.WaitforAll();
        while (state.completed.load(std::memory_order_acquire) < n) {
            ts_.WaitforTask(nullptr);
            std::this_thread::yield();
        }
        for (TaskId i = 0; i < n; ++i) ts_.WaitforTask(&tasks[i]);

        if (state.error) std::rethrow_exception(state.error);
    }

private:
    enki::TaskScheduler ts_;
};

}  // namespace

std::unique_ptr<Scheduler> make_enkits_scheduler(std::uint32_t threads) {
    return std::make_unique<EnkiScheduler>(threads);
}
bool enkits_available() { return true; }

}  // namespace isc::sched

#else  // !ISC_HAS_ENKITS

namespace isc::sched {
std::unique_ptr<Scheduler> make_enkits_scheduler(std::uint32_t) { return nullptr; }
bool enkits_available() { return false; }
}  // namespace isc::sched

#endif
