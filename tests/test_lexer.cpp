#include "isc/lang/lexer.hpp"
#include "isc_test.hpp"

using namespace isc::lang;

ISC_TEST(lexer_tokenizes_readme_example) {
    Diagnostics d;
    auto t = tokenize("!float {\n    precision = fp32;\n}\n", d);
    CHECK(!d.has_errors());
    CHECK_EQ(t.size(), 8u);
    CHECK(t[0].kind == TokenKind::Directive);
    CHECK_EQ(t[0].text, std::string("float"));
    CHECK(t[1].kind == TokenKind::LBrace);
    CHECK(t[2].kind == TokenKind::Identifier);
    CHECK_EQ(t[2].text, std::string("precision"));
    CHECK(t[3].kind == TokenKind::Equals);
    CHECK(t[4].kind == TokenKind::Identifier);
    CHECK(t[5].kind == TokenKind::Semicolon);
    CHECK(t[6].kind == TokenKind::RBrace);
    CHECK(t[7].kind == TokenKind::EndOfFile);
}

ISC_TEST(lexer_numbers) {
    Diagnostics d;
    auto t = tokenize("1 -2 0.25 .5 1e-3 2.5E+2", d);
    CHECK(!d.has_errors());
    CHECK(t[0].kind == TokenKind::Integer);
    CHECK_NEAR(t[0].number, 1.0, 0.0);
    CHECK(t[1].kind == TokenKind::Integer);
    CHECK_NEAR(t[1].number, -2.0, 0.0);
    CHECK(t[2].kind == TokenKind::Float);
    CHECK_NEAR(t[2].number, 0.25, 1e-12);
    CHECK(t[3].kind == TokenKind::Float);
    CHECK_NEAR(t[3].number, 0.5, 1e-12);
    CHECK(t[4].kind == TokenKind::Float);
    CHECK_NEAR(t[4].number, 0.001, 1e-12);
    CHECK(t[5].kind == TokenKind::Float);
    CHECK_NEAR(t[5].number, 250.0, 1e-9);
    CHECK(t[6].kind == TokenKind::EndOfFile);
}

ISC_TEST(lexer_comments_and_locations) {
    Diagnostics d;
    auto t = tokenize("// line comment\n/* block\n comment */ !artifact", d);
    CHECK(!d.has_errors());
    CHECK(t[0].kind == TokenKind::Directive);
    CHECK_EQ(t[0].loc.line, 3u);
    CHECK_EQ(t[0].loc.column, 13u);
}

ISC_TEST(lexer_strings_and_escapes) {
    Diagnostics d;
    auto t = tokenize(R"("a\"b\\c")", d);
    CHECK(!d.has_errors());
    CHECK(t[0].kind == TokenKind::String);
    CHECK_EQ(t[0].text, std::string("a\"b\\c"));
}

ISC_TEST(lexer_reports_errors) {
    {
        Diagnostics d;
        tokenize("@", d);
        CHECK(d.has_errors());
    }
    {
        Diagnostics d;
        tokenize("! float", d);
        CHECK(d.has_errors());
    }
    {
        Diagnostics d;
        tokenize("\"open", d);
        CHECK(d.has_errors());
    }
    {
        Diagnostics d;
        tokenize("/* never closed", d);
        CHECK(d.has_errors());
    }
    {
        Diagnostics d;
        tokenize("12abc", d);
        CHECK(d.has_errors());
    }
}

ISC_TEST(lexer_skips_utf8_bom) {
    Diagnostics d;
    auto t = tokenize("\xEF\xBB\xBFrender", d);
    CHECK(!d.has_errors());
    CHECK(t[0].kind == TokenKind::Identifier);
    CHECK_EQ(t[0].loc.column, 1u);
}
