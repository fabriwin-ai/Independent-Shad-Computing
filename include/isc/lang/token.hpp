#pragma once

#include <string>
#include <string_view>

#include "isc/lang/diagnostics.hpp"

namespace isc::lang {

enum class TokenKind {
    Directive,   // !float, !artifact  (text holds the name without '!')
    Identifier,  // render, profile, fp32, enabled, ...
    Integer,
    Float,
    String,      // "..." (text holds the unescaped contents)
    LBrace,
    RBrace,
    Equals,
    Semicolon,
    EndOfFile,
    Invalid,
};

std::string_view to_string(TokenKind k);

struct Token {
    TokenKind kind = TokenKind::Invalid;
    std::string text;
    double number = 0.0;
    SourceLocation loc;
};

}  // namespace isc::lang
