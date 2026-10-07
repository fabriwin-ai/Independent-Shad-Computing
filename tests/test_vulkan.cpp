#include <cmath>
#include <memory>
#include <string>

#include "isc/runtime.hpp"
#include "isc/util/image_io.hpp"
#include "isc_test.hpp"

using namespace isc;

namespace {

const char* kReadme = R"(
!float { precision = fp32; accumulation = fp32; }
!artifact { source = framebuffer; gradient = enabled; iterations = 4; weight = 0.25; }
render { input = framebuffer; output = display; }
)";

// Creates a Vulkan runtime or skips the test with the reason.
std::unique_ptr<Runtime> vulkan_runtime_or_skip(const char* src) {
    if (!vulkan_backend_compiled()) SKIP("built without the Vulkan backend");
    RuntimeOptions o;
    o.backend = BackendKind::Vulkan;
    auto rt = std::make_unique<Runtime>(o);
    std::string report;
    if (!rt->load(src, "vk.fa", &report)) SKIP(("no usable Vulkan device: " + report).c_str());
    return rt;
}

std::unique_ptr<Runtime> cpu_runtime(const char* src) {
    RuntimeOptions o;
    o.backend = BackendKind::Cpu;
    auto rt = std::make_unique<Runtime>(o);
    if (!rt->load(src)) isc_test::fail(__FILE__, __LINE__, "cpu load failed");
    return rt;
}

// GPU vs CPU reference on one frame at a pinned rung.
void compare(const char* src, Lod lod, float scale, std::uint32_t w, std::uint32_t h, double tol) {
    auto vk = vulkan_runtime_or_skip(src);
    auto cpu = cpu_runtime(src);
    CHECK(vk->controller().force(lod, scale));
    CHECK(cpu->controller().force(lod, scale));

    RenderTarget in(w, h), out_vk, out_cpu;
    util::fill_test_pattern(in, 7);
    FrameStats sv, sc;
    std::string err;
    CHECK(cpu->process(in, out_cpu, 0.0, &sc, &err));
    if (!vk->process(in, out_vk, 0.0, &sv, &err)) isc_test::fail(__FILE__, __LINE__, "vulkan process failed: " + err);

    CHECK_EQ(sv.stages.size(), sc.stages.size());
    for (std::size_t i = 0; i < sv.stages.size(); ++i) {
        CHECK_NEAR(sv.stages[i].weight_out, sc.stages[i].weight_out, 1e-3);
        CHECK_EQ(sv.stages[i].iterations, sc.stages[i].iterations);
    }
    const double diff = max_abs_diff(out_vk, out_cpu);
    if (!(diff <= tol)) {
        isc_test::fail(__FILE__, __LINE__,
                       "GPU/CPU max abs diff " + std::to_string(diff) + " exceeds tolerance " + std::to_string(tol));
    }
}

}  // namespace

ISC_TEST(vulkan_device_caps) {
    auto rt = vulkan_runtime_or_skip(kReadme);
    const DeviceCaps& caps = rt->backend()->caps();
    CHECK(caps.backend == BackendKind::Vulkan);
    CHECK(!caps.name.empty());
    CHECK(caps.max_workgroup_invocations >= 256u);
    CHECK(caps.device_local_bytes > 0u);
}

ISC_TEST(vulkan_bypass_is_identity) {
    auto rt = vulkan_runtime_or_skip(kReadme);
    CHECK(rt->controller().force(Lod::Bypass, 0.5f));
    RenderTarget in(40, 30), out;
    util::fill_test_pattern(in);
    CHECK(rt->process(in, out));
    CHECK_NEAR(max_abs_diff(in, out), 0.0, 0.0);
}

ISC_TEST(vulkan_matches_cpu_lod1) { compare(kReadme, Lod::Basic, 1.0f, 97, 61, 1e-4); }

ISC_TEST(vulkan_matches_cpu_lod3_gradient) { compare(kReadme, Lod::Iterative, 1.0f, 128, 80, 2e-3); }

ISC_TEST(vulkan_matches_cpu_dynamic_resolution) { compare(kReadme, Lod::Extended, 0.5f, 160, 90, 2e-3); }

ISC_TEST(vulkan_matches_cpu_chain) {
    compare("!artifact soft { radius = 3; weight = 0.3; } !artifact fine { source = soft; gradient = enabled; "
            "iterations = 8; weight = 0.2; } render { }",
            Lod::Iterative, 0.75f, 120, 68, 3e-3);
}

ISC_TEST(vulkan_runs_continuously_and_reuses_buffers) {
    auto rt = vulkan_runtime_or_skip(kReadme);
    RenderTarget in(160, 90), out;
    FrameStats st;
    std::string err;
    for (std::uint32_t f = 0; f < 60; ++f) {
        util::fill_test_pattern(in, f);
        if (!rt->process(in, out, 0.0, &st, &err)) isc_test::fail(__FILE__, __LINE__, err);
    }
    CHECK_EQ(rt->frame_index(), 60u);
    CHECK(st.memory.cache_hits > 0);
    CHECK(st.memory.reserved_bytes <= st.memory.budget_bytes);
    CHECK(st.memory.blocks >= 1u);
}
