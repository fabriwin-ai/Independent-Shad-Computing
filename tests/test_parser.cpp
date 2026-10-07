#include "isc/lang/parser.hpp"
#include "isc_test.hpp"

using namespace isc::lang;

static const char* kReadme = R"(
!float {
    precision = fp32;
    accumulation = fp32;
}

!artifact {
    source = framebuffer;
    gradient = enabled;
    iterations = 4;
    weight = 0.25;
}

render {
    input = framebuffer;
    output = display;
}
)";

ISC_TEST(parser_readme_example) {
    auto r = parse(kReadme);
    CHECK(r.ok());
    CHECK_EQ(r.program.blocks.size(), 3u);
    CHECK(r.program.blocks[0].kind == BlockKind::Float);
    CHECK(r.program.blocks[1].kind == BlockKind::Artifact);
    CHECK(r.program.blocks[2].kind == BlockKind::Render);
    CHECK_EQ(r.program.blocks[1].fields.size(), 4u);
    const Field* w = r.program.blocks[1].find("weight");
    CHECK(w != nullptr);
    CHECK(w->value.kind == Value::Kind::Float);
    CHECK_NEAR(w->value.number, 0.25, 1e-12);
}

ISC_TEST(parser_named_blocks) {
    auto r = parse("!artifact sharpen { radius = 2; } profile gen11 { target_fps = 30; } render { }");
    CHECK(r.ok());
    CHECK_EQ(r.program.blocks.size(), 3u);
    CHECK_EQ(r.program.blocks[0].name, std::string("sharpen"));
    CHECK(r.program.blocks[1].kind == BlockKind::Profile);
    CHECK_EQ(r.program.blocks[1].name, std::string("gen11"));
}

ISC_TEST(parser_recovers_from_errors) {
    auto r = parse("!artifact { weight = ; iterations = 4; }\nrender { input = framebuffer output = display; }");
    CHECK(!r.ok());
    CHECK(r.diagnostics.error_count() >= 2);
    CHECK_EQ(r.program.blocks.size(), 2u);
    CHECK(r.program.blocks[0].find("iterations") != nullptr);
    CHECK_EQ(r.program.blocks[1].fields.size(), 2u);
}

ISC_TEST(parser_unknown_directive) {
    auto r = parse("!shader { a = 1; }\nrender { }");
    CHECK(!r.ok());
    CHECK(r.diagnostics.all()[0].message.find("unknown directive") != std::string::npos);
    CHECK_EQ(r.program.blocks.size(), 1u);
    CHECK(r.program.blocks[0].kind == BlockKind::Render);
}

ISC_TEST(parser_unterminated_block) {
    auto r = parse("!float { precision = fp32;");
    CHECK(!r.ok());
    CHECK(r.diagnostics.all().back().message.find("unterminated") != std::string::npos);
}

ISC_TEST(parser_missing_close_before_next_block) {
    auto r = parse("!float { precision = fp32;\n!artifact { weight = 0.5; }\nrender { }");
    CHECK(!r.ok());
    CHECK_EQ(r.program.blocks.size(), 3u);
}

ISC_TEST(parser_error_location) {
    auto r = parse("render {\n  input = = ;\n}");
    CHECK(!r.ok());
    CHECK_EQ(r.diagnostics.all()[0].loc.line, 2u);
}

ISC_TEST(parser_rejects_stray_tokens) {
    auto r = parse("42 render { }");
    CHECK(!r.ok());
    CHECK_EQ(r.program.blocks.size(), 1u);
}
