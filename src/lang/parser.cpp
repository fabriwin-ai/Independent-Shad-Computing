#include "isc/lang/parser.hpp"

#include <utility>

#include "isc/lang/lexer.hpp"

namespace isc::lang {
namespace {

class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diags) : toks_(std::move(tokens)), diags_(diags) {}

    Program parse_program() {
        Program program;
        while (!check(TokenKind::EndOfFile)) {
            parse_declaration(program);
        }
        return program;
    }

private:
    const Token& peek(std::size_t ahead = 0) const {
        const std::size_t i = pos_ + ahead;
        return i < toks_.size() ? toks_[i] : toks_.back();
    }
    const Token& advance() {
        const Token& t = peek();
        if (pos_ < toks_.size() - 1) ++pos_;
        return t;
    }
    bool check(TokenKind k) const { return peek().kind == k; }

    static bool starts_declaration(const Token& t) {
        return t.kind == TokenKind::Directive ||
               (t.kind == TokenKind::Identifier && (t.text == "render" || t.text == "profile"));
    }

    void parse_declaration(Program& program) {
        const Token& head = peek();
        Block block;
        block.loc = head.loc;
        bool keep = true;

        if (head.kind == TokenKind::Directive) {
            if (head.text == "float") {
                block.kind = BlockKind::Float;
            } else if (head.text == "artifact") {
                block.kind = BlockKind::Artifact;
            } else {
                diags_.error(head.loc, "unknown directive '!" + head.text + "' (expected !float or !artifact)");
                keep = false;  // still parse the body to recover cleanly
            }
            advance();
        } else if (head.kind == TokenKind::Identifier && head.text == "render") {
            block.kind = BlockKind::Render;
            advance();
        } else if (head.kind == TokenKind::Identifier && head.text == "profile") {
            block.kind = BlockKind::Profile;
            advance();
        } else {
            diags_.error(head.loc, "expected a declaration (!float, !artifact, render, profile), found " +
                                       describe(head));
            advance();
            // Skip to something that can start a declaration.
            while (!check(TokenKind::EndOfFile) && !starts_declaration(peek())) advance();
            return;
        }

        if (check(TokenKind::Identifier)) {
            block.name = advance().text;
        } else if (check(TokenKind::String)) {
            block.name = advance().text;
        }

        if (!check(TokenKind::LBrace)) {
            diags_.error(peek().loc, "expected '{' to open " + std::string(to_string(block.kind)) +
                                         " block, found " + describe(peek()));
            while (!check(TokenKind::EndOfFile) && !check(TokenKind::LBrace) && !starts_declaration(peek()))
                advance();
            if (!check(TokenKind::LBrace)) return;
        }
        advance();  // '{'

        while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
            if (starts_declaration(peek()) && peek(1).kind != TokenKind::Equals) {
                diags_.error(block.loc, "missing '}' to close " + std::string(to_string(block.kind)) + " block");
                if (keep) program.blocks.push_back(std::move(block));
                return;
            }
            parse_field(block);
        }
        if (check(TokenKind::EndOfFile)) {
            diags_.error(block.loc, "unterminated " + std::string(to_string(block.kind)) + " block, expected '}'");
        } else {
            advance();  // '}'
        }
        if (keep) program.blocks.push_back(std::move(block));
    }

    void parse_field(Block& block) {
        if (!check(TokenKind::Identifier)) {
            diags_.error(peek().loc, "expected a field name, found " + describe(peek()));
            sync_to_field_end();
            return;
        }
        Field field;
        field.loc = peek().loc;
        field.key = advance().text;

        if (!check(TokenKind::Equals)) {
            diags_.error(peek().loc, "expected '=' after '" + field.key + "', found " + describe(peek()));
            sync_to_field_end();
            return;
        }
        advance();

        const Token& v = peek();
        Value value;
        value.loc = v.loc;
        value.text = v.text;
        value.number = v.number;
        switch (v.kind) {
            case TokenKind::Identifier: value.kind = Value::Kind::Identifier; break;
            case TokenKind::Integer: value.kind = Value::Kind::Integer; break;
            case TokenKind::Float: value.kind = Value::Kind::Float; break;
            case TokenKind::String: value.kind = Value::Kind::String; break;
            default:
                diags_.error(v.loc, "expected a value for '" + field.key + "', found " + describe(v));
                sync_to_field_end();
                return;
        }
        advance();
        field.value = std::move(value);

        if (check(TokenKind::Semicolon)) {
            advance();
        } else {
            diags_.error(peek().loc, "expected ';' after the value of '" + field.key + "'");
            // Keep the field; resynchronise only if the next token cannot start a field.
            if (!check(TokenKind::Identifier) && !check(TokenKind::RBrace)) sync_to_field_end();
        }
        block.fields.push_back(std::move(field));
    }

    void sync_to_field_end() {
        while (!check(TokenKind::EndOfFile) && !check(TokenKind::RBrace)) {
            if (check(TokenKind::Semicolon)) {
                advance();
                return;
            }
            advance();
        }
    }

    static std::string describe(const Token& t) {
        switch (t.kind) {
            case TokenKind::Identifier: return "identifier '" + t.text + "'";
            case TokenKind::Directive: return "directive '!" + t.text + "'";
            case TokenKind::Integer:
            case TokenKind::Float: return "number '" + t.text + "'";
            case TokenKind::String: return "string \"" + t.text + "\"";
            default: return std::string(to_string(t.kind));
        }
    }

    std::vector<Token> toks_;
    Diagnostics& diags_;
    std::size_t pos_ = 0;
};

}  // namespace

ParseResult parse(std::string_view source) {
    ParseResult result;
    std::vector<Token> tokens = tokenize(source, result.diagnostics);
    // Invalid tokens were already reported by the lexer; drop them so the
    // parser does not report the same problem twice.
    std::vector<Token> filtered;
    filtered.reserve(tokens.size());
    for (auto& t : tokens)
        if (t.kind != TokenKind::Invalid) filtered.push_back(std::move(t));
    Parser parser(std::move(filtered), result.diagnostics);
    result.program = parser.parse_program();
    return result;
}

}  // namespace isc::lang
