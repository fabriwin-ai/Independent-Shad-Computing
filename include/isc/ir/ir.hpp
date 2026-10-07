#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "isc/core/profile.hpp"
#include "isc/core/types.hpp"
#include "isc/lang/ast.hpp"
#include "isc/lang/diagnostics.hpp"

namespace isc::ir {

// Resource reference meaning "the captured host framebuffer".
constexpr std::uint32_t kFramebuffer = 0xFFFFFFFFu;

// !float
struct Numerics {
    Precision precision = Precision::FP32;     // per-pixel arithmetic
    Precision accumulation = Precision::FP32;  // reductions / loss accumulation
    bool declared = false;
    lang::SourceLocation loc;
};

// !artifact - one enhancement stage. The reference operator is a detail
// (unsharp) enhancement: out = in + weight * (in - box_blur_radius(in)).
// With `gradient = enabled`, `weight` is the *initial* value of a parameter
// optimised every frame so that the output's edge energy approaches
// `target` x the source's edge energy (see docs/LANGUAGE.md).
struct Artifact {
    std::string name;
    std::uint32_t source = kFramebuffer;  // artifact index or kFramebuffer
    bool gradient = false;
    std::uint32_t iterations = 1;
    float weight = 0.25f;
    float learning_rate = 0.25f;
    float target = 1.25f;
    float weight_min = 0.0f;
    float weight_max = 4.0f;
    std::uint32_t radius = 1;
    lang::SourceLocation loc;
};

// render
//   input = framebuffer  -> the framebuffer enhanced by the terminal artifact
//                           chain (the artifact no other artifact consumes;
//                           the last declared one if several)
//   input = <artifact>   -> that artifact's output
struct Render {
    std::uint32_t input = kFramebuffer;   // as declared
    std::uint32_t result = kFramebuffer;  // resolved artifact shown on `output`
    std::string output = "display";
    BackendKind backend = BackendKind::Auto;
    std::string profile_name = "balanced";
    lang::SourceLocation loc;
};

struct Module {
    Numerics numerics;
    std::vector<Artifact> artifacts;   // declaration order
    Render render;
    RenderProfile profile;             // resolved built-in + overrides (not yet device-tuned)
    std::vector<std::uint32_t> order;  // artifacts feeding render.input, sources first

    std::uint32_t find_artifact(std::string_view name) const;
    std::string resource_name(std::uint32_t ref) const;
};

struct CompileResult {
    Module module;
    lang::Diagnostics diagnostics;
    bool ok() const { return !diagnostics.has_errors(); }
};

// AST -> IR with full semantic validation. Returns false on errors.
bool lower(const lang::Program& program, Module& out, lang::Diagnostics& diags);

// parse + lower.
CompileResult compile(std::string_view source);

std::string dump(const Module& module);

}  // namespace isc::ir
