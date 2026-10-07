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

namespace isc_test {
// A function (not `const auto& v = (a);` inside the macro) so that temporaries
// in the operands - e.g. `*cache.take(key)` on a temporary std::optional - live
// until the comparison and the message are done (end of the full-expression).
template <class A, class B>
void check_eq(const A& a, const B& b, const char* expr_a, const char* expr_b, const char* file, int line) {
    if (!(a == b)) {
        std::ostringstream os;
        os << "CHECK_EQ(" << expr_a << ", " << expr_b << ") failed: " << a << " != " << b;
        fail(file, line, os.str());
    }
}
}  // namespace isc_test

#define CHECK_EQ(a, b) ::isc_test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)

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
