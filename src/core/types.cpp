#include "isc/core/types.hpp"

namespace isc {

std::string_view to_string(Precision p) {
    switch (p) {
        case Precision::FP16: return "fp16";
        case Precision::FP32: return "fp32";
        case Precision::FP64: return "fp64";
    }
    return "?";
}

std::string_view to_string(Lod l) {
    switch (l) {
        case Lod::Bypass: return "LOD0-bypass";
        case Lod::Basic: return "LOD1-basic";
        case Lod::Extended: return "LOD2-extended";
        case Lod::Iterative: return "LOD3-iterative";
    }
    return "?";
}

std::string_view to_string(Ownership o) {
    switch (o) {
        case Ownership::Host: return "host";
        case Ownership::Cpu: return "cpu";
        case Ownership::Gpu: return "gpu";
        case Ownership::InTransition: return "in-transition";
    }
    return "?";
}

std::string_view to_string(BackendKind b) {
    switch (b) {
        case BackendKind::Auto: return "auto";
        case BackendKind::Cpu: return "cpu";
        case BackendKind::Vulkan: return "vulkan";
    }
    return "?";
}

bool parse_precision(std::string_view text, Precision& out) {
    if (text == "fp16") { out = Precision::FP16; return true; }
    if (text == "fp32") { out = Precision::FP32; return true; }
    if (text == "fp64") { out = Precision::FP64; return true; }
    return false;
}

bool parse_backend(std::string_view text, BackendKind& out) {
    if (text == "auto") { out = BackendKind::Auto; return true; }
    if (text == "cpu") { out = BackendKind::Cpu; return true; }
    if (text == "vulkan") { out = BackendKind::Vulkan; return true; }
    return false;
}

}  // namespace isc
