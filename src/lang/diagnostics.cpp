#include "isc/lang/diagnostics.hpp"

#include <sstream>

namespace isc::lang {

void Diagnostics::error(SourceLocation loc, std::string message) {
    items_.push_back({Severity::Error, loc, std::move(message)});
    ++errors_;
}

void Diagnostics::warning(SourceLocation loc, std::string message) {
    items_.push_back({Severity::Warning, loc, std::move(message)});
    ++warnings_;
}

void Diagnostics::note(SourceLocation loc, std::string message) {
    items_.push_back({Severity::Note, loc, std::move(message)});
}

void Diagnostics::append(const Diagnostics& other) {
    for (const auto& d : other.items_) {
        items_.push_back(d);
        if (d.severity == Severity::Error) ++errors_;
        if (d.severity == Severity::Warning) ++warnings_;
    }
}

std::string Diagnostics::format(std::string_view file_name) const {
    std::ostringstream s;
    for (const auto& d : items_) {
        const char* sev = d.severity == Severity::Error ? "error" : d.severity == Severity::Warning ? "warning" : "note";
        s << file_name << ':' << d.loc.line << ':' << d.loc.column << ": " << sev << ": " << d.message << '\n';
    }
    return s.str();
}

}  // namespace isc::lang
