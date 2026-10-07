#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

#include "isc/sched/task_graph.hpp"

namespace isc::sched {

// fn(begin, end, thread): process items [begin, end). `thread` is a stable
// worker index < thread_count(), intended for per-thread scratch buckets.
using RangeFn = std::function<void(std::uint32_t begin, std::uint32_t end, std::uint32_t thread)>;

class Scheduler {
public:
    virtual ~Scheduler() = default;

    virtual std::string_view name() const = 0;
    virtual std::uint32_t thread_count() const = 0;

    // Data-parallel loop. May be called from inside a running task.
    virtual void parallel_for(std::uint32_t count, std::uint32_t min_range, const RangeFn& fn) = 0;

    // Runs every task respecting dependencies; independent tasks may run
    // concurrently. Throws std::runtime_error on a cycle and rethrows the
    // first exception raised by a task after the graph has drained.
    virtual void run(const TaskGraph& graph) = 0;
};

std::unique_ptr<Scheduler> make_serial_scheduler();

// nullptr when the library was built without enkiTS (ISC_WITH_ENKITS=OFF).
// threads = 0 -> hardware concurrency.
std::unique_ptr<Scheduler> make_enkits_scheduler(std::uint32_t threads = 0);

// enkiTS if available, otherwise serial.
std::unique_ptr<Scheduler> make_default_scheduler(std::uint32_t threads = 0);

bool enkits_available();

}  // namespace isc::sched
