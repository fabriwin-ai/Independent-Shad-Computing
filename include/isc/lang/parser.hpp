#pragma once

#include <string_view>

#include "isc/lang/ast.hpp"
#include "isc/lang/diagnostics.hpp"

namespace isc::lang {

// Grammar (see docs/LANGUAGE.md):
//
//   program     := declaration* EOF
//   declaration := header [identifier] "{" field* "}"
//   header      := "!float" | "!artifact" | "render" | "profile"
//   field       := identifier "=" value ";"
//   value       := identifier | integer | float | string
//
// The parser is purely syntactic; key names, types and ranges are checked
// when lowering to IR. It recovers from errors at ';' and '}' so one pass
// reports as many problems as possible.
struct ParseResult {
    Program program;
    Diagnostics diagnostics;
    bool ok() const { return !diagnostics.has_errors(); }
};

ParseResult parse(std::string_view source);

}  // namespace isc::lang
