#include "isc/core/render_target.hpp"
#include "isc/ir/ir.hpp"
#include "isc/lod/adaptive_controller.hpp"
#include "isc/plan/execution_plan.hpp"
#include "isc_test.hpp"

using namespace isc;

static ir::Module readme_module() {
    auto r = ir::compile(R"(
        !float { precision = fp32; accumulation = fp32; }
        !artifact { source = framebuffer; gradient = enabled; iterations = 4; weight = 0.25; }
        render { input = framebuffer; output = display; }
    )");
    return r.module;
}

ISC_TEST(plan_lod_mapping) {
    const ir::Module m = readme_module();
    CHECK(plan::make_plan(m, Lod::Bypass, 1.0f, 64, 64).bypass());
    auto l1 = plan::make_plan(m, Lod::Basic, 1.0f, 64, 64);
    CHECK_EQ(l1.stages.size(), 1u);
    CHECK(!l1.stages[0].gradient);
    auto l2 = plan::make_plan(m, Lod::Extended, 1.0f, 64, 64);
    CHECK(l2.stages[0].gradient);
    CHECK_EQ(l2.stages[0].iterations, 1u);
    auto l3 = plan::make_plan(m, Lod::Iterative, 1.0f, 64, 64);
    CHECK_EQ(l3.stages[0].iterations, 4u);
}

ISC_TEST(plan_scaled_extent) {
    CHECK_EQ(plan::scaled_extent(1920, 0.5f), 960u);
    CHECK_EQ(plan::scaled_extent(1080, 0.75f), 810u);
    CHECK_EQ(plan::scaled_extent(1, 0.5f), 1u);
    auto p = plan::make_plan(readme_module(), Lod::Basic, 0.5f, 1920, 1080);
    CHECK(p.scaled());
    CHECK_EQ(p.work_width, 960u);
    CHECK_EQ(p.work_height, 540u);
}

ISC_TEST(plan_memory_estimate_and_cap) {
    const ir::Module m = readme_module();
    const std::uint64_t full = RenderTarget::bytes_for(1920, 1080);
    CHECK_EQ(plan::estimate_frame_bytes(plan::make_plan(m, Lod::Basic, 1.0f, 1920, 1080)), 4 * full);
    const float s = plan::max_scale_for_budget(m, Lod::Basic, 1920, 1080, 100 * kMiB, 0.5f, 1.0f, 0.125f);
    CHECK_NEAR(s, 0.5f, 1e-6);
    const float big = plan::max_scale_for_budget(m, Lod::Basic, 1920, 1080, 1024 * kMiB, 0.5f, 1.0f, 0.125f);
    CHECK_NEAR(big, 1.0f, 1e-6);
}

ISC_TEST(lod_ladder_balanced) {
    RenderProfile p;  // balanced
    lod::AdaptiveController c(p);
    CHECK_EQ(c.rung_count(), 16u);  // bypass + 3 LODs x 5 scales
    CHECK(c.lod() == Lod::Extended);
    CHECK_NEAR(c.resolution_scale(), 1.0f, 1e-6);
}

ISC_TEST(lod_steps_down_resolution_first_when_over_budget) {
    RenderProfile p;
    lod::AdaptiveController c(p);
    CHECK(!c.update(10.0));  // 1st over-budget frame
    CHECK(c.update(10.0));   // 2nd -> step down
    CHECK(c.lod() == Lod::Extended);
    CHECK_NEAR(c.resolution_scale(), 0.875f, 1e-6);
    for (int i = 0; i < 8; ++i) c.update(10.0);
    CHECK(c.lod() == Lod::Basic);  // resolution exhausted -> LOD drops
}

ISC_TEST(lod_steps_up_after_hysteresis) {
    RenderProfile p;
    p.hysteresis_frames = 5;
    lod::AdaptiveController c(p);
    CHECK(c.force(Lod::Basic, 0.5f));
    for (int i = 0; i < 4; ++i) CHECK(!c.update(0.5));
    CHECK(c.update(0.5));
    CHECK(c.lod() == Lod::Basic);
    CHECK_NEAR(c.resolution_scale(), 0.625f, 1e-6);
}

ISC_TEST(lod_dead_band_prevents_oscillation) {
    RenderProfile p;
    p.hysteresis_frames = 3;
    lod::AdaptiveController c(p);
    c.force(Lod::Basic, 0.5f);
    const std::size_t start = c.rung();
    for (int i = 0; i < 20; ++i) {
        c.update(0.5);  // under
        c.update(3.5);  // dead band (0.875 of budget) resets the counter
    }
    CHECK_EQ(c.rung(), start);
}

ISC_TEST(lod_pinned_rung_ignores_load_but_reports_it) {
    RenderProfile p;
    p.hysteresis_frames = 1;
    lod::AdaptiveController c(p);
    CHECK(c.force(Lod::Basic, 0.75f));
    c.set_pinned(true);
    const std::size_t pinned = c.rung();
    for (int i = 0; i < 10; ++i) CHECK(!c.update(40.0));  // 10x over budget
    for (int i = 0; i < 10; ++i) CHECK(!c.update(0.0));   // far under budget
    CHECK_EQ(c.rung(), pinned);
    c.update(8.0);
    CHECK_NEAR(c.last_load(), 2.0, 1e-9);  // load still measured (8 ms / 4 ms budget)
    c.set_pinned(false);
    CHECK(c.update(0.0));  // adaptation resumes (hysteresis_frames = 1)
    CHECK(c.rung() == pinned + 1);
}

ISC_TEST(lod_host_frame_time_counts) {
    RenderProfile p;  // 60 fps -> 16.6 ms
    lod::AdaptiveController c(p);
    c.update(0.1, 25.0);
    c.update(0.1, 25.0);
    CHECK_NEAR(c.resolution_scale(), 0.875f, 1e-6);
}

ISC_TEST(lod_memory_cap_forces_lower_scale) {
    RenderProfile p;
    lod::AdaptiveController c(p);
    c.set_scale_cap(0.6f);
    CHECK(c.lod() == Lod::Extended);
    CHECK_NEAR(c.resolution_scale(), 0.5f, 1e-6);
    p.hysteresis_frames = 1;
    c.reset(p);
    c.set_scale_cap(0.6f);
    for (int i = 0; i < 10; ++i) c.update(0.0);
    CHECK(c.resolution_scale() <= 0.6f);  // never climbs above the cap ...
    CHECK(c.lod() == Lod::Iterative);     // ... but still climbs in LOD
}

ISC_TEST(lod_fixed_when_adaptivity_disabled) {
    RenderProfile p;
    p.adaptive_lod = false;
    p.dynamic_resolution = false;
    lod::AdaptiveController c(p);
    CHECK_EQ(c.rung_count(), 1u);
    c.update(100.0);
    c.update(100.0);
    CHECK(c.lod() == Lod::Extended);
}
