#include <algorithm>
#include <string>

#include "isc/runtime.hpp"
#include "isc/util/image_io.hpp"
#include "isc_test.hpp"

using namespace isc;

static const char* kReadme = R"(
!float { precision = fp32; accumulation = fp32; }
!artifact { source = framebuffer; gradient = enabled; iterations = 4; weight = 0.25; }
render { input = framebuffer; output = display; }
)";

// Mean squared forward-difference gradient over RGB (same definition as the loss).
static double edge_energy(const RenderTarget& img) {
    double s = 0.0;
    const std::uint32_t w = img.width(), h = img.height();
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const float* p = img.pixel(x, y);
            const float* px = img.pixel(std::min(x + 1, w - 1), y);
            const float* py = img.pixel(x, std::min(y + 1, h - 1));
            for (int c = 0; c < 3; ++c) {
                const double gx = px[c] - p[c], gy = py[c] - p[c];
                s += gx * gx + gy * gy;
            }
        }
    return s / (static_cast<double>(w) * h * 3.0);
}

ISC_TEST(cpu_bypass_is_identity) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    CHECK(rt.load(kReadme));
    CHECK(rt.controller().force(Lod::Bypass, 0.5f));
    RenderTarget in(64, 48), out;
    util::fill_test_pattern(in);
    FrameStats st;
    CHECK(rt.process(in, out, 0.0, &st));
    CHECK(st.lod == Lod::Bypass);
    CHECK_NEAR(max_abs_diff(in, out), 0.0, 0.0);
}

ISC_TEST(cpu_enhancement_increases_edge_energy) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    CHECK(rt.load(kReadme));
    CHECK(rt.controller().force(Lod::Basic, 1.0f));
    RenderTarget in(96, 64), out;
    util::fill_test_pattern(in);
    FrameStats st;
    CHECK(rt.process(in, out, 0.0, &st));
    CHECK(st.lod == Lod::Basic);
    CHECK_EQ(st.stages.size(), 1u);
    CHECK_NEAR(st.stages[0].weight_out, 0.25, 1e-7);  // no gradient at LOD1
    CHECK(max_abs_diff(in, out) > 1e-3);
    CHECK(edge_energy(out) > edge_energy(in));
}

ISC_TEST(cpu_gradient_optimises_declared_weight) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    CHECK(rt.load(kReadme));
    CHECK(rt.controller().force(Lod::Iterative, 1.0f));
    RenderTarget in(96, 64), out;
    util::fill_test_pattern(in);
    FrameStats st;
    CHECK(rt.process(in, out, 0.0, &st));
    CHECK_EQ(st.stages.size(), 1u);
    CHECK(st.stages[0].iterations >= 1u);
    CHECK(st.stages[0].weight_out != st.stages[0].weight_in);
    CHECK(st.stages[0].loss_final < st.stages[0].loss_initial);
}

ISC_TEST(cpu_deterministic_across_schedulers_and_runs) {
    RenderTarget in(80, 60);
    util::fill_test_pattern(in, 3);
    RenderTarget a, b, c;
    {
        RuntimeOptions o;
        o.backend = BackendKind::Cpu;
        o.threads = 1;
        Runtime rt(o);
        CHECK(rt.load(kReadme));
        rt.controller().force(Lod::Iterative, 1.0f);
        CHECK(rt.process(in, a));
        rt.controller().force(Lod::Iterative, 1.0f);
        CHECK(rt.process(in, c));
    }
    {
        RuntimeOptions o;
        o.backend = BackendKind::Cpu;
        Runtime rt(o);  // default thread count
        CHECK(rt.load(kReadme));
        rt.controller().force(Lod::Iterative, 1.0f);
        CHECK(rt.process(in, b));
    }
    CHECK_NEAR(max_abs_diff(a, b), 0.0, 0.0);
    CHECK_NEAR(max_abs_diff(a, c), 0.0, 0.0);
}

ISC_TEST(cpu_dynamic_resolution_path) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    CHECK(rt.load(kReadme));
    CHECK(rt.controller().force(Lod::Extended, 0.5f));
    RenderTarget in(128, 72), out;
    util::fill_test_pattern(in);
    FrameStats st;
    CHECK(rt.process(in, out, 0.0, &st));
    CHECK_EQ(st.work_width, 64u);
    CHECK_EQ(st.work_height, 36u);
    CHECK_EQ(out.width(), 128u);
    CHECK(edge_energy(out) > edge_energy(in));
}

ISC_TEST(cpu_chain_of_two_artifacts) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    std::string report;
    CHECK(rt.load("!artifact soft { radius = 2; weight = 0.2; } !artifact fine { source = soft; radius = 1; "
                  "weight = 0.1; } render { }",
                  "chain.fa", &report));
    rt.controller().force(Lod::Basic, 1.0f);
    RenderTarget in(64, 64), out;
    util::fill_test_pattern(in);
    FrameStats st;
    CHECK(rt.process(in, out, 0.0, &st));
    CHECK_EQ(st.stages.size(), 2u);
    CHECK_EQ(st.stages[0].name, std::string("soft"));
    CHECK_EQ(st.stages[1].name, std::string("fine"));
}

ISC_TEST(cpu_precision_fp64_close_to_fp32) {
    RenderTarget in(64, 48), a, b;
    util::fill_test_pattern(in);
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime r32(o), r64(o);
    CHECK(r32.load(kReadme));
    CHECK(r64.load("!float { precision = fp64; accumulation = fp64; } !artifact { gradient = enabled; iterations = 4; "
                   "weight = 0.25; } render { }"));
    r32.controller().force(Lod::Iterative, 1.0f);
    r64.controller().force(Lod::Iterative, 1.0f);
    CHECK(r32.process(in, a));
    CHECK(r64.process(in, b));
    CHECK(max_abs_diff(a, b) < 1e-4);
}

ISC_TEST(cpu_runs_continuously_and_reuses_buffers) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    CHECK(rt.load(kReadme));
    RenderTarget in(64, 48), out;
    FrameStats st;
    for (std::uint32_t f = 0; f < 120; ++f) {
        util::fill_test_pattern(in, f);
        CHECK(rt.process(in, out, 0.0, &st));
    }
    CHECK_EQ(rt.frame_index(), 120u);
    CHECK(st.memory.cache_hits > 0);
    CHECK(st.memory.used_bytes <= st.memory.budget_bytes);
}

ISC_TEST(cpu_load_reports_errors) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    Runtime rt(o);
    std::string report;
    CHECK(!rt.load("!artifact { weight = banana; } render { }", "bad.fa", &report));
    CHECK(report.find("bad.fa:1:") != std::string::npos);
    RenderTarget in(8, 8), out;
    std::string err;
    CHECK(!rt.process(in, out, 0.0, nullptr, &err));
}
