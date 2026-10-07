#include <sstream>

#include "isc/lang/ast.hpp"

namespace isc::lang {

std::string_view to_string(BlockKind k) {
    switch (k) {
        case BlockKind::Float: return "!float";
        case BlockKind::Artifact: return "!artifact";
        case BlockKind::Render: return "render";
        case BlockKind::Profile: return "profile";
    }
    return "?";
}

std::string dump(const Program& program) {
    std::ostringstream s;
    for (const auto& b : program.blocks) {
        s << to_string(b.kind);
        if (!b.name.empty()) s << ' ' << b.name;
        s << " {  // line " << b.loc.line << '\n';
        for (const auto& f : b.fields) {
            s << "    " << f.key << " = ";
            if (f.value.kind == Value::Kind::String) {
                s << '"' << f.value.text << '"';
            } else {
                s << f.value.text;
            }
            s << ";\n";
        }
        s << "}\n";
    }
    return s.str();
}

}  // namespace isc::lang
