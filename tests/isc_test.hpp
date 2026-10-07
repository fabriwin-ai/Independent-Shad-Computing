#pragma once

// Minimal self-registering test harness (no third-party dependency).

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

namespace isc_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Failure {
    std::string message;
};
struct Skipped {
    std::string reason;
};

[[noreturn]] inline void fail(const char* file, int line, const std::string& msg) {
    std::ostringstream s;
    s << file << ':' << line << ": " << msg;
    throw Failure{s.str()};
}

}  // namespace isc_test

#define ISC_TEST(name)                                                      \
    static void name();                                                     \
    static ::isc_test::Registrar name##_registrar(#name, &name);            \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) ::isc_test::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        const auto& va_ = (a);                                                             \
        const auto& vb_ = (b);                                                             \
        if (!(va_ == vb_)) {                                                               \
            std::ostringstream os_;                                                        \
            os_ << "CHECK_EQ(" #a ", " #b ") failed: " << va_ << " != " << vb_;            \
            ::isc_test::fail(__FILE__, __LINE__, os_.str());                               \
        }                                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                        \
    do {                                                                                             \
        const double va_ = static_cast<double>(a);                                                   \
        const double vb_ = static_cast<double>(b);                                                   \
        if (!(std::fabs(va_ - vb_) <= (eps))) {                                                      \
            std::ostringstream os_;                                                                  \
            os_ << "CHECK_NEAR(" #a ", " #b ", " #eps ") failed: " << va_ << " vs " << vb_;          \
            ::isc_test::fail(__FILE__, __LINE__, os_.str());                                         \
        }                                                                                            \
    } while (0)

#define SKIP(reason) throw ::isc_test::Skipped{reason}
