#include "isc/ir/ir.hpp"
#include "isc_test.hpp"

using namespace isc;

static bool has_message(const lang::Diagnostics& d, const std::string& needle) {
    for (const auto& m : d.all())
        if (m.message.find(needle) != std::string::npos) return true;
    return false;
}

ISC_TEST(ir_readme_example_lowers) {
    auto r = ir::compile(R"(
        !float { precision = fp32; accumulation = fp32; }
        !artifact { source = framebuffer; gradient = enabled; iterations = 4; weight = 0.25; }
        render { input = framebuffer; output = display; }
    )");
    CHECK(r.ok());
    CHECK_EQ(r.module.artifacts.size(), 1u);
    const auto& a = r.module.artifacts[0];
    CHECK(a.gradient);
    CHECK_EQ(a.iterations, 4u);
    CHECK_NEAR(a.weight, 0.25, 1e-7);
    CHECK(a.source == ir::kFramebuffer);
    CHECK_EQ(r.module.order.size(), 1u);
    CHECK_EQ(r.module.render.result, 0u);
    CHECK(r.module.numerics.declared);
    CHECK_EQ(r.module.profile.name, std::string("balanced"));
}

ISC_TEST(ir_rejects_unknown_keys) {
    auto r = ir::compile("!artifact { colour = red; } render { }");
    CHECK(!r.ok());
    CHECK(has_message(r.diagnostics, "unknown field 'colour'"));
}

ISC_TEST(ir_type_and_range_errors) {
    CHECK(!ir::compile("!artifact { iterations = 1000; } render { }").ok());
    CHECK(!ir::compile("!artifact { weight = fast; } render { }").ok());
    CHECK(!ir::compile("!artifact { radius = 2.5; } render { }").ok());
    CHECK(!ir::compile("!float { precision = fp8; } render { }").ok());
    CHECK(!ir::compile("!artifact { weight = 5; weight_max = 4; } render { }").ok());
    CHECK(!ir::compile("render { backend = metal; }").ok());
    CHECK(!ir::compile("render { output = file; }").ok());
}

ISC_TEST(ir_duplicate_fields_and_blocks) {
    auto r = ir::compile("!artifact a { weight = 0.1; weight = 0.2; } !artifact a { } !float { } !float { } render { }");
    CHECK(!r.ok());
    CHECK(has_message(r.diagnostics, "duplicate field 'weight'"));
    CHECK(has_message(r.diagnostics, "duplicate artifact name 'a'"));
    CHECK(has_message(r.diagnostics, "only one !float"));
}

ISC_TEST(ir_unknown_source_and_cycles) {
    auto bad = ir::compile("!artifact a { source = nope; } render { input = a; }");
    CHECK(!bad.ok());
    CHECK(has_message(bad.diagnostics, "unknown resource 'nope'"));

    auto cyc = ir::compile("!artifact a { source = b; } !artifact b { source = a; } render { input = a; }");
    CHECK(!cyc.ok());
    CHECK(has_message(cyc.diagnostics, "cycle"));
}

ISC_TEST(ir_chain_order_and_terminal_resolution) {
    auto r = ir::compile("!artifact a { } !artifact b { source = a; } render { input = framebuffer; }");
    CHECK(r.ok());
    CHECK_EQ(r.module.order.size(), 2u);
    CHECK_EQ(r.module.order[0], 0u);
    CHECK_EQ(r.module.order[1], 1u);
    CHECK_EQ(r.module.render.result, 1u);

    auto explicit_input = ir::compile("!artifact a { } !artifact b { source = a; } render { input = a; }");
    CHECK(explicit_input.ok());
    CHECK_EQ(explicit_input.module.order.size(), 1u);
    CHECK(has_message(explicit_input.diagnostics, "does not contribute"));
}

ISC_TEST(ir_missing_render_block) {
    auto r = ir::compile("!float { }");
    CHECK(!r.ok());
    CHECK(has_message(r.diagnostics, "missing render block"));
}

ISC_TEST(ir_no_artifacts_is_bypass) {
    auto r = ir::compile("render { input = framebuffer; output = display; }");
    CHECK(r.ok());
    CHECK(r.module.order.empty());
}

ISC_TEST(ir_profile_block_overrides_builtin) {
    auto r = ir::compile("profile quality { buffer_cache_mb = 32; } render { profile = quality; }");
    CHECK(r.ok());
    CHECK_EQ(r.module.profile.name, std::string("quality"));
    CHECK(r.module.profile.lod_start == Lod::Iterative);
    CHECK_EQ(r.module.profile.buffer_cache_mb, 32u);
}

ISC_TEST(ir_custom_profile_with_base_used_implicitly) {
    auto r = ir::compile(
        "profile gen11 { base = performance; target_fps = 30; lod_max = iterative; dynamic_resolution = disabled; "
        "resolution_scale_start = 0.75; } render { }");
    CHECK(r.ok());
    CHECK_EQ(r.module.profile.name, std::string("gen11"));
    CHECK_NEAR(r.module.profile.enhance_budget_ms, 2.0, 1e-9);
    CHECK_NEAR(r.module.profile.target_fps, 30.0, 1e-9);
    CHECK(r.module.profile.lod_max == Lod::Iterative);
    CHECK(!r.module.profile.dynamic_resolution);
}

ISC_TEST(ir_invalid_and_unknown_profiles) {
    auto inv = ir::compile("profile x { resolution_scale_min = 0.9; resolution_scale_max = 0.5; } render { }");
    CHECK(!inv.ok());
    auto unk = ir::compile("render { profile = turbo; }");
    CHECK(!unk.ok());
    CHECK(has_message(unk.diagnostics, "unknown profile 'turbo'"));
    auto unnamed = ir::compile("profile { target_fps = 30; } render { }");
    CHECK(!unnamed.ok());
}

ISC_TEST(ir_dump_mentions_everything) {
    auto r = ir::compile("!artifact sharpen { gradient = enabled; } render { }");
    CHECK(r.ok());
    const std::string s = ir::dump(r.module);
    CHECK(s.find("sharpen") != std::string::npos);
    CHECK(s.find("profile balanced") != std::string::npos);
}
