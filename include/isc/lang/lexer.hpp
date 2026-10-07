#pragma once

#include <string_view>
#include <vector>

#include "isc/lang/diagnostics.hpp"
#include "isc/lang/token.hpp"

namespace isc::lang {

// Lexical rules (.fa sources):
//   whitespace         ignored
//   // ...             line comment
//   /* ... */          block comment (not nested)
//   !name              directive
//   [A-Za-z_][A-Za-z0-9_]*                 identifier
//   -?[0-9]+                               integer
//   -?[0-9]*.[0-9]+([eE][+-]?[0-9]+)?      float (also 1e-3, 2.)
//   "..."              string with \" \\ \n \t escapes
//   { } = ;            punctuation
class Lexer {
public:
    Lexer(std::string_view source, Diagnostics& diags);
    Token next();

private:
    char peek(std::size_t ahead = 0) const;
    char advance();
    bool at_end() const { return pos_ >= src_.size(); }
    void skip_trivia();
    Token make(TokenKind kind, SourceLocation loc, std::string text = {});
    Token lex_number(SourceLocation loc);
    Token lex_string(SourceLocation loc);

    std::string_view src_;
    Diagnostics& diags_;
    std::size_t pos_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t col_ = 1;
};

// Tokenizes the whole source; the last token is always EndOfFile.
std::vector<Token> tokenize(std::string_view source, Diagnostics& diags);

}  // namespace isc::lang
