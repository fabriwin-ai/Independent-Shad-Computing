#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace isc::sched {

enum class TaskPriority : std::uint8_t { High, Normal, Low };

using TaskId = std::uint32_t;
using TaskFn = std::function<void()>;

// Directed acyclic graph of tasks for one frame. Built by the CPU backend
// from an ExecutionPlan, executed by a Scheduler:
//
//   Capture -> { Float-stats || Artifact-detail } -> Gradient -> Composite -> Display
class TaskGraph {
public:
    TaskId add(std::string name, TaskFn fn, TaskPriority priority = TaskPriority::Normal);
    // `task` runs only after `prerequisite` has completed.
    void depend(TaskId task, TaskId prerequisite);
    void clear() { nodes_.clear(); }

    std::size_t size() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }
    const std::string& name(TaskId id) const { return nodes_[id].name; }
    TaskPriority priority(TaskId id) const { return nodes_[id].priority; }
    const std::vector<TaskId>& prerequisites(TaskId id) const { return nodes_[id].pre; }
    const std::vector<TaskId>& dependents(TaskId id) const { return nodes_[id].post; }
    void run_task(TaskId id) const {
        if (nodes_[id].fn) nodes_[id].fn();
    }

    // Kahn's algorithm, ties broken by priority then insertion order, so the
    // serial order is deterministic. Returns false if the graph has a cycle.
    bool topological_order(std::vector<TaskId>& out) const;

    // Groups of mutually independent tasks ("waves"); every task's
    // prerequisites live in earlier waves. Empty if the graph has a cycle.
    std::vector<std::vector<TaskId>> levels() const;

    std::string describe() const;

private:
    struct Node {
        std::string name;
        TaskFn fn;
        TaskPriority priority;
        std::vector<TaskId> pre;
        std::vector<TaskId> post;
    };
    std::vector<Node> nodes_;
};

}  // namespace isc::sched
