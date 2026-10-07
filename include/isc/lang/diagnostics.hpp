#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace isc::lang {

struct SourceLocation {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t offset = 0;
};

enum class Severity : std::uint8_t { Note, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLocation loc;
    std::string message;
};

class Diagnostics {
public:
    void error(SourceLocation loc, std::string message);
    void warning(SourceLocation loc, std::string message);
    void note(SourceLocation loc, std::string message);
    void append(const Diagnostics& other);

    bool has_errors() const { return errors_ > 0; }
    std::size_t error_count() const { return errors_; }
    std::size_t warning_count() const { return warnings_; }
    const std::vector<Diagnostic>& all() const { return items_; }
    bool empty() const { return items_.empty(); }

    // "file:line:col: error: message" per line.
    std::string format(std::string_view file_name) const;

private:
    std::vector<Diagnostic> items_;
    std::size_t errors_ = 0;
    std::size_t warnings_ = 0;
};

}  // namespace isc::lang
