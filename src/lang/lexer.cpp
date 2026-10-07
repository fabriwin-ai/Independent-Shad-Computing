#include "isc/lang/lexer.hpp"

#include <locale>
#include <sstream>

namespace isc::lang {
namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

double parse_double(const std::string& text) {
    std::istringstream ss(text);
    ss.imbue(std::locale::classic());  // never depend on the host locale's decimal point
    double v = 0.0;
    ss >> v;
    return v;
}

}  // namespace

std::string_view to_string(TokenKind k) {
    switch (k) {
        case TokenKind::Directive: return "directive";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::Integer: return "integer";
        case TokenKind::Float: return "float";
        case TokenKind::String: return "string";
        case TokenKind::LBrace: return "'{'";
        case TokenKind::RBrace: return "'}'";
        case TokenKind::Equals: return "'='";
        case TokenKind::Semicolon: return "';'";
        case TokenKind::EndOfFile: return "end of file";
        case TokenKind::Invalid: return "invalid token";
    }
    return "?";
}

Lexer::Lexer(std::string_view source, Diagnostics& diags) : src_(source), diags_(diags) {
    // Skip a UTF-8 byte-order mark written by some Windows editors.
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF) {
        pos_ = 3;
    }
}

char Lexer::peek(std::size_t ahead) const {
    const std::size_t i = pos_ + ahead;
    return i < src_.size() ? src_[i] : '\0';
}

char Lexer::advance() {
    const char c = src_[pos_++];
    if (c == '\n') {
        ++line_;
        col_ = 1;
    } else {
        ++col_;
    }
    return c;
}

void Lexer::skip_trivia() {
    for (;;) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            const SourceLocation loc{line_, col_, static_cast<std::uint32_t>(pos_)};
            advance();
            advance();
            bool closed = false;
            while (!at_end()) {
                if (peek() == '*' && peek(1) == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) diags_.error(loc, "unterminated block comment");
        } else {
            return;
        }
    }
}

Token Lexer::make(TokenKind kind, SourceLocation loc, std::string text) {
    Token t;
    t.kind = kind;
    t.loc = loc;
    t.text = std::move(text);
    return t;
}

Token Lexer::lex_number(SourceLocation loc) {
    std::string text;
    bool is_float = false;
    if (peek() == '-') text += advance();
    while (is_digit(peek())) text += advance();
    if (peek() == '.') {
        is_float = true;
        text += advance();
        while (is_digit(peek())) text += advance();
    }
    if (peek() == 'e' || peek() == 'E') {
        const char n1 = peek(1);
        if (is_digit(n1) || ((n1 == '+' || n1 == '-') && is_digit(peek(2)))) {
            is_float = true;
            text += advance();
            if (peek() == '+' || peek() == '-') text += advance();
            while (is_digit(peek())) text += advance();
        }
    }
    if (is_ident_char(peek())) {
        while (is_ident_char(peek())) text += advance();
        diags_.error(loc, "invalid numeric literal '" + text + "'");
        return make(TokenKind::Invalid, loc, text);
    }
    Token t = make(is_float ? TokenKind::Float : TokenKind::Integer, loc, text);
    t.number = parse_double(text);
    return t;
}

Token Lexer::lex_string(SourceLocation loc) {
    advance();  // opening quote
    std::string value;
    while (!at_end() && peek() != '"' && peek() != '\n') {
        char c = advance();
        if (c == '\\' && !at_end()) {
            const char e = advance();
            switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default:
                    diags_.warning(loc, std::string("unknown escape sequence '\\") + e + "'");
                    c = e;
                    break;
            }
        }
        value += c;
    }
    if (peek() != '"') {
        diags_.error(loc, "unterminated string literal");
        return make(TokenKind::Invalid, loc, value);
    }
    advance();  // closing quote
    return make(TokenKind::String, loc, value);
}

Token Lexer::next() {
    skip_trivia();
    const SourceLocation loc{line_, col_, static_cast<std::uint32_t>(pos_)};
    if (at_end()) return make(TokenKind::EndOfFile, loc);

    const char c = peek();
    switch (c) {
        case '{': advance(); return make(TokenKind::LBrace, loc, "{");
        case '}': advance(); return make(TokenKind::RBrace, loc, "}");
        case '=': advance(); return make(TokenKind::Equals, loc, "=");
        case ';': advance(); return make(TokenKind::Semicolon, loc, ";");
        case '"': return lex_string(loc);
        default: break;
    }

    if (c == '!') {
        advance();
        if (!is_ident_start(peek())) {
            diags_.error(loc, "expected a directive name after '!'");
            return make(TokenKind::Invalid, loc, "!");
        }
        std::string name;
        while (is_ident_char(peek())) name += advance();
        return make(TokenKind::Directive, loc, name);
    }

    if (is_ident_start(c)) {
        std::string name;
        while (is_ident_char(peek())) name += advance();
        return make(TokenKind::Identifier, loc, name);
    }

    const bool starts_number = is_digit(c) || (c == '.' && is_digit(peek(1))) ||
                               (c == '-' && (is_digit(peek(1)) || (peek(1) == '.' && is_digit(peek(2)))));
    if (starts_number) return lex_number(loc);

    advance();
    std::string shown(1, c);
    if (static_cast<unsigned char>(c) < 0x20) shown = "\\x" + std::to_string(static_cast<int>(c));
    diags_.error(loc, "unexpected character '" + shown + "'");
    return make(TokenKind::Invalid, loc, shown);
}

std::vector<Token> tokenize(std::string_view source, Diagnostics& diags) {
    Lexer lexer(source, diags);
    std::vector<Token> tokens;
    for (;;) {
        Token t = lexer.next();
        const bool eof = t.kind == TokenKind::EndOfFile;
        tokens.push_back(std::move(t));
        if (eof) break;
    }
    return tokens;
}

}  // namespace isc::lang
