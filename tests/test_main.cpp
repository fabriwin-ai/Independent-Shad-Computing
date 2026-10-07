#include <cstdio>
#include <cstring>
#include <exception>

#include "isc_test.hpp"

// Usage: isc_tests [name-prefix]   e.g. `isc_tests parser`
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0, skipped = 0;
    for (const auto& c : isc_test::registry()) {
        if (filter && std::strncmp(c.name, filter, std::strlen(filter)) != 0) continue;
        try {
            c.fn();
            std::printf("[ PASS ] %s\n", c.name);
            ++passed;
        } catch (const isc_test::Skipped& s) {
            std::printf("[ SKIP ] %s - %s\n", c.name, s.reason.c_str());
            ++skipped;
        } catch (const isc_test::Failure& f) {
            std::printf("[ FAIL ] %s\n         %s\n", c.name, f.message.c_str());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("[ FAIL ] %s\n         unexpected exception: %s\n", c.name, e.what());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed, %d skipped\n", passed, failed, skipped);
    return failed == 0 ? 0 : 1;
}
