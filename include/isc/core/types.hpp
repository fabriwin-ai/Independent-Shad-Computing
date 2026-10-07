#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace isc {

using FrameIndex = std::uint64_t;

// Numerical precision for !float (precision / accumulation).
enum class Precision : std::uint8_t { FP16, FP32, FP64 };

// Level of detail controls computational cost, never artistic detail (LOD.md).
enum class Lod : std::uint8_t { Bypass = 0, Basic = 1, Extended = 2, Iterative = 3 };

// Who currently owns a resource. Transitions are explicit so that host, CPU
// and GPU never touch the same memory without a synchronization point.
enum class Ownership : std::uint8_t { Host, Cpu, Gpu, InTransition };

enum class BackendKind : std::uint8_t { Auto, Cpu, Vulkan };

std::string_view to_string(Precision p);
std::string_view to_string(Lod l);
std::string_view to_string(Ownership o);
std::string_view to_string(BackendKind b);

bool parse_precision(std::string_view text, Precision& out);
bool parse_backend(std::string_view text, BackendKind& out);

// Generational handle for pooled resources.
struct ResourceHandle {
    static constexpr std::uint32_t kInvalid = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t index = kInvalid;
    std::uint32_t generation = 0;

    bool valid() const { return index != kInvalid; }
    friend bool operator==(ResourceHandle a, ResourceHandle b) {
        return a.index == b.index && a.generation == b.generation;
    }
    friend bool operator!=(ResourceHandle a, ResourceHandle b) { return !(a == b); }
};

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

}  // namespace isc
