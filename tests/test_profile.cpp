#include "isc/core/profile.hpp"
#include "isc_test.hpp"

using namespace isc;

ISC_TEST(profile_builtins_are_valid) {
    for (auto name : builtin_profile_names()) {
        RenderProfile p;
        CHECK(builtin_profile(name, p));
        CHECK_EQ(validate_profile(p), std::string());
    }
    RenderProfile p;
    CHECK(!builtin_profile("turbo", p));
}

ISC_TEST(profile_tuning_intel_gen11_uma) {
    const DeviceCaps gpu = intel_hd_graphics_gen11_caps();
    RenderProfile balanced;
    builtin_profile("balanced", balanced);
    auto t = tune_profile_for_device(balanced, gpu);
    CHECK_EQ(t.profile.memory_budget_mb, 256u);  // fits 10% of the 3.89 GiB shared heap
    CHECK(t.notes.empty());

    RenderProfile quality;
    builtin_profile("quality", quality);
    quality.frames_in_flight = 3;
    auto q = tune_profile_for_device(quality, gpu);
    CHECK_EQ(q.profile.memory_budget_mb, 398u);  // 512 -> 10% of heap
    CHECK_EQ(q.profile.frames_in_flight, 2u);    // single queue
    CHECK_EQ(q.profile.buffer_cache_mb, 128u);
    CHECK(q.notes.size() >= 2);
    CHECK_EQ(validate_profile(q.profile), std::string());
}

ISC_TEST(profile_tuning_respects_memory_budget_extension) {
    DeviceCaps gpu = intel_hd_graphics_gen11_caps();
    gpu.budget_bytes = 400 * kMiB;
    RenderProfile balanced;
    builtin_profile("balanced", balanced);
    auto t = tune_profile_for_device(balanced, gpu);
    CHECK_EQ(t.profile.memory_budget_mb, 200u);  // <= 50% of the reported budget
    CHECK(t.profile.buffer_cache_mb <= 100u);
    CHECK(t.profile.memory_block_mb <= t.profile.memory_budget_mb);
}

ISC_TEST(profile_tuning_cpu_is_synchronous) {
    RenderProfile p;
    auto t = tune_profile_for_device(p, cpu_device_caps());
    CHECK_EQ(t.profile.frames_in_flight, 1u);
}

ISC_TEST(profile_validation_catches_bad_values) {
    RenderProfile p;
    p.lod_start = Lod::Bypass;
    p.lod_min = Lod::Basic;
    CHECK(!validate_profile(p).empty());
    RenderProfile q;
    q.buffer_cache_mb = q.memory_budget_mb + 1;
    CHECK(!validate_profile(q).empty());
    RenderProfile r;
    r.resolution_scale_max = 1.5f;
    CHECK(!validate_profile(r).empty());
}

ISC_TEST(profile_device_caps_precision) {
    const DeviceCaps gpu = intel_hd_graphics_gen11_caps();
    CHECK(gpu.supports(Precision::FP16));
    CHECK(gpu.supports(Precision::FP32));
    CHECK(!gpu.supports(Precision::FP64));
    CHECK(gpu.summary().find("Gen11") != std::string::npos);
}
