#include "isc/sched/task_graph.hpp"

#include <algorithm>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace isc::sched {

TaskId TaskGraph::add(std::string name, TaskFn fn, TaskPriority priority) {
    nodes_.push_back(Node{std::move(name), std::move(fn), priority, {}, {}});
    return static_cast<TaskId>(nodes_.size() - 1);
}

void TaskGraph::depend(TaskId task, TaskId prerequisite) {
    if (task >= nodes_.size() || prerequisite >= nodes_.size()) throw std::out_of_range("TaskGraph::depend: bad id");
    auto& pre = nodes_[task].pre;
    if (std::find(pre.begin(), pre.end(), prerequisite) != pre.end()) return;  // idempotent
    pre.push_back(prerequisite);
    nodes_[prerequisite].post.push_back(task);
}

bool TaskGraph::topological_order(std::vector<TaskId>& out) const {
    out.clear();
    const std::size_t n = nodes_.size();
    std::vector<std::size_t> pending(n);
    // (priority, id) min-heap: High(0) first, then lower ids first.
    using Item = std::pair<int, TaskId>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> ready;
    for (TaskId i = 0; i < n; ++i) {
        pending[i] = nodes_[i].pre.size();
        if (pending[i] == 0) ready.emplace(static_cast<int>(nodes_[i].priority), i);
    }
    while (!ready.empty()) {
        const TaskId id = ready.top().second;
        ready.pop();
        out.push_back(id);
        for (TaskId next : nodes_[id].post) {
            if (--pending[next] == 0) ready.emplace(static_cast<int>(nodes_[next].priority), next);
        }
    }
    return out.size() == n;
}

std::vector<std::vector<TaskId>> TaskGraph::levels() const {
    std::vector<TaskId> order;
    if (!topological_order(order)) return {};
    std::vector<std::size_t> level(nodes_.size(), 0);
    std::size_t max_level = 0;
    for (TaskId id : order) {
        for (TaskId p : nodes_[id].pre) level[id] = std::max(level[id], level[p] + 1);
        max_level = std::max(max_level, level[id]);
    }
    std::vector<std::vector<TaskId>> waves(order.empty() ? 0 : max_level + 1);
    for (TaskId id : order) waves[level[id]].push_back(id);
    return waves;
}

std::string TaskGraph::describe() const {
    std::ostringstream s;
    const auto waves = levels();
    for (std::size_t w = 0; w < waves.size(); ++w) {
        s << "  wave " << w << ":";
        for (TaskId id : waves[w]) s << ' ' << nodes_[id].name;
        s << '\n';
    }
    return s.str();
}

}  // namespace isc::sched
