#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <vector>

#include "isc/sched/scheduler.hpp"
#include "isc_test.hpp"

using namespace isc::sched;

ISC_TEST(scheduler_topological_order_respects_dependencies) {
    TaskGraph g;
    const TaskId a = g.add("a", {});
    const TaskId b = g.add("b", {});
    const TaskId c = g.add("c", {});
    const TaskId d = g.add("d", {});
    g.depend(b, a);
    g.depend(c, a);
    g.depend(d, b);
    g.depend(d, c);
    std::vector<TaskId> order;
    CHECK(g.topological_order(order));
    auto pos = [&](TaskId t) { return std::find(order.begin(), order.end(), t) - order.begin(); };
    CHECK(pos(a) < pos(b));
    CHECK(pos(a) < pos(c));
    CHECK(pos(b) < pos(d));
    CHECK(pos(c) < pos(d));
}

ISC_TEST(scheduler_levels_match_readme_parallel_model) {
    // Capture -> {Float, Artifact} -> Gradient -> Composite -> Display
    TaskGraph g;
    const TaskId cap = g.add("capture", {});
    const TaskId fl = g.add("float", {});
    const TaskId art = g.add("artifact", {});
    const TaskId grad = g.add("gradient", {});
    const TaskId comp = g.add("composite", {});
    const TaskId disp = g.add("display", {});
    g.depend(fl, cap);
    g.depend(art, cap);
    g.depend(grad, fl);
    g.depend(grad, art);
    g.depend(comp, grad);
    g.depend(disp, comp);
    const auto waves = g.levels();
    CHECK_EQ(waves.size(), 5u);
    CHECK_EQ(waves[1].size(), 2u);  // float || artifact
}

ISC_TEST(scheduler_detects_cycle) {
    TaskGraph g;
    const TaskId a = g.add("a", {});
    const TaskId b = g.add("b", {});
    g.depend(a, b);
    g.depend(b, a);
    std::vector<TaskId> order;
    CHECK(!g.topological_order(order));
    CHECK(g.levels().empty());
    auto s = make_serial_scheduler();
    bool threw = false;
    try {
        s->run(g);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static void check_graph_execution(Scheduler& s) {
    // Wide fan-out / fan-in graph; every task verifies its prerequisites ran.
    constexpr int kLayers = 6, kWidth = 8;
    TaskGraph g;
    std::vector<std::atomic<int>> done(kLayers * kWidth + 2);
    for (auto& d : done) d.store(0);
    std::atomic<int> violations{0};

    std::vector<TaskId> ids;
    const TaskId root = g.add("root", [&] { done[0] = 1; });
    ids.push_back(root);
    std::vector<TaskId> prev{root};
    for (int l = 0; l < kLayers; ++l) {
        std::vector<TaskId> layer;
        for (int w = 0; w < kWidth; ++w) {
            const int slot = 1 + l * kWidth + w;
            std::vector<TaskId> pres = prev;
            const TaskId t = g.add("t", [&, slot, pres] {
                for (TaskId p : pres)
                    if (done[p].load() == 0) ++violations;
                done[slot] = 1;
            });
            for (TaskId p : prev) g.depend(t, p);
            layer.push_back(t);
        }
        prev = layer;
    }
    const int last = kLayers * kWidth + 1;
    const TaskId sink = g.add("sink", [&] { done[last] = 1; });
    for (TaskId p : prev) g.depend(sink, p);

    s.run(g);
    CHECK_EQ(violations.load(), 0);
    for (auto& d : done) CHECK_EQ(d.load(), 1);
}

ISC_TEST(scheduler_serial_runs_graph) {
    auto s = make_serial_scheduler();
    check_graph_execution(*s);
}

ISC_TEST(scheduler_default_runs_graph) {
    auto s = make_default_scheduler();
    check_graph_execution(*s);
    check_graph_execution(*s);  // reusable across frames
}

ISC_TEST(scheduler_enkits_if_available) {
    if (!enkits_available()) SKIP("built without enkiTS (ISC_WITH_ENKITS=OFF)");
    auto s = make_enkits_scheduler(4);
    CHECK(s != nullptr);
    CHECK_EQ(std::string(s->name()), std::string("enkiTS"));
    check_graph_execution(*s);
}

ISC_TEST(scheduler_parallel_for_covers_range) {
    auto s = make_default_scheduler();
    std::vector<int> hits(10007, 0);
    std::atomic<long long> sum{0};
    s->parallel_for(static_cast<std::uint32_t>(hits.size()), 64, [&](std::uint32_t b, std::uint32_t e, std::uint32_t) {
        long long local = 0;
        for (std::uint32_t i = b; i < e; ++i) {
            hits[i] += 1;
            local += i;
        }
        sum += local;
    });
    for (int h : hits) CHECK_EQ(h, 1);
    CHECK_EQ(sum.load(), static_cast<long long>(10006) * 10007 / 2);
}

ISC_TEST(scheduler_propagates_exceptions) {
    auto s = make_default_scheduler();
    TaskGraph g;
    g.add("boom", [] { throw std::runtime_error("boom"); });
    bool threw = false;
    try {
        s->run(g);
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()) == "boom";
    }
    CHECK(threw);
}

ISC_TEST(scheduler_priority_breaks_ties_serially) {
    auto s = make_serial_scheduler();
    std::vector<int> seq;
    TaskGraph g;
    g.add("low", [&] { seq.push_back(1); }, TaskPriority::Low);
    g.add("high", [&] { seq.push_back(2); }, TaskPriority::High);
    s->run(g);
    CHECK_EQ(seq.size(), 2u);
    CHECK_EQ(seq[0], 2);
}
