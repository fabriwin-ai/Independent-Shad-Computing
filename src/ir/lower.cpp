#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <sstream>

#include "isc/ir/ir.hpp"
#include "isc/lang/parser.hpp"

namespace isc::ir {
namespace {

using lang::Block;
using lang::BlockKind;
using lang::Diagnostics;
using lang::Field;
using lang::Value;

std::string fmt(double v) {
    std::ostringstream s;
    s << v;
    return s.str();
}

std::string describe(const Value& v) {
    switch (v.kind) {
        case Value::Kind::Identifier: return "identifier '" + v.text + "'";
        case Value::Kind::String: return "string \"" + v.text + "\"";
        default: return "number " + v.text;
    }
}

std::string join(const std::vector<std::string_view>& items) {
    std::string s;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) s += ", ";
        s += items[i];
    }
    return s;
}

// Typed accessors that report precise diagnostics.
struct Reader {
    Diagnostics& d;

    bool name(const Field& f, std::string& out) {
        if (f.value.kind != Value::Kind::Identifier && f.value.kind != Value::Kind::String) {
            d.error(f.value.loc, "'" + f.key + "' expects a name, found " + describe(f.value));
            return false;
        }
        out = f.value.text;
        return true;
    }

    bool boolean(const Field& f, bool& out) {
        const std::string& t = f.value.text;
        if (f.value.kind == Value::Kind::Identifier) {
            if (t == "enabled" || t == "true" || t == "on") { out = true; return true; }
            if (t == "disabled" || t == "false" || t == "off") { out = false; return true; }
        }
        d.error(f.value.loc, "'" + f.key + "' expects enabled|disabled, found " + describe(f.value));
        return false;
    }

    bool uinteger(const Field& f, std::uint64_t lo, std::uint64_t hi, std::uint64_t& out) {
        if (f.value.kind != Value::Kind::Integer) {
            d.error(f.value.loc, "'" + f.key + "' expects an integer, found " + describe(f.value));
            return false;
        }
        if (f.value.number < static_cast<double>(lo) || f.value.number > static_cast<double>(hi)) {
            d.error(f.value.loc, "'" + f.key + "' must be in [" + std::to_string(lo) + ", " + std::to_string(hi) +
                                     "], got " + f.value.text);
            return false;
        }
        out = static_cast<std::uint64_t>(f.value.number);
        return true;
    }

    template <class T>
    bool uint_as(const Field& f, std::uint64_t lo, std::uint64_t hi, T& out) {
        std::uint64_t v = 0;
        if (!uinteger(f, lo, hi, v)) return false;
        out = static_cast<T>(v);
        return true;
    }

    bool real(const Field& f, double lo, double hi, double& out) {
        if (!f.value.is_number()) {
            d.error(f.value.loc, "'" + f.key + "' expects a number, found " + describe(f.value));
            return false;
        }
        if (!(f.value.number >= lo && f.value.number <= hi)) {
            d.error(f.value.loc, "'" + f.key + "' must be in [" + fmt(lo) + ", " + fmt(hi) + "], got " + f.value.text);
            return false;
        }
        out = f.value.number;
        return true;
    }

    template <class T>
    bool real_as(const Field& f, double lo, double hi, T& out) {
        double v = 0.0;
        if (!real(f, lo, hi, v)) return false;
        out = static_cast<T>(v);
        return true;
    }

    bool precision(const Field& f, Precision& out) {
        if (f.value.kind == Value::Kind::Identifier && parse_precision(f.value.text, out)) return true;
        d.error(f.value.loc, "'" + f.key + "' expects fp16|fp32|fp64, found " + describe(f.value));
        return false;
    }

    bool lod(const Field& f, Lod& out) {
        if (f.value.kind == Value::Kind::Integer && f.value.number >= 0 && f.value.number <= 3) {
            out = static_cast<Lod>(static_cast<int>(f.value.number));
            return true;
        }
        if (f.value.kind == Value::Kind::Identifier) {
            const std::string& t = f.value.text;
            if (t == "bypass") { out = Lod::Bypass; return true; }
            if (t == "basic") { out = Lod::Basic; return true; }
            if (t == "extended") { out = Lod::Extended; return true; }
            if (t == "iterative") { out = Lod::Iterative; return true; }
        }
        d.error(f.value.loc, "'" + f.key + "' expects 0..3 or bypass|basic|extended|iterative, found " +
                                 describe(f.value));
        return false;
    }
};

// Reports unknown and duplicate keys; returns false if any were found.
bool check_fields(const Block& b, const std::vector<std::string_view>& allowed, Diagnostics& d) {
    bool ok = true;
    std::map<std::string, lang::SourceLocation> seen;
    for (const auto& f : b.fields) {
        if (std::find(allowed.begin(), allowed.end(), f.key) == allowed.end()) {
            d.error(f.loc, "unknown field '" + f.key + "' in " + std::string(lang::to_string(b.kind)) +
                               " block (expected one of: " + join(allowed) + ")");
            ok = false;
            continue;
        }
        auto [it, inserted] = seen.emplace(f.key, f.loc);
        if (!inserted) {
            d.error(f.loc, "duplicate field '" + f.key + "' (first set on line " + std::to_string(it->second.line) + ")");
            ok = false;
        }
    }
    return ok;
}

const std::vector<std::string_view> kFloatKeys = {"precision", "accumulation"};
const std::vector<std::string_view> kArtifactKeys = {"source",        "gradient", "iterations", "weight",
                                                     "learning_rate", "target",   "weight_min", "weight_max",
                                                     "radius"};
const std::vector<std::string_view> kRenderKeys = {"input", "output", "backend", "profile"};
const std::vector<std::string_view> kProfileKeys = {
    "base",
    "target_fps",
    "enhance_budget_ms",
    "adaptive_lod",
    "lod_min",
    "lod_max",
    "lod_start",
    "dynamic_resolution",
    "resolution_scale_min",
    "resolution_scale_max",
    "resolution_scale_step",
    "resolution_scale_start",
    "upscale_threshold",
    "hysteresis_frames",
    "downscale_frames",
    "memory_budget_mb",
    "memory_block_mb",
    "buffer_cache_mb",
    "frames_in_flight",
};

void lower_float(const Block& b, Numerics& n, Reader& r) {
    check_fields(b, kFloatKeys, r.d);
    n.declared = true;
    n.loc = b.loc;
    if (const Field* f = b.find("precision")) r.precision(*f, n.precision);
    if (const Field* f = b.find("accumulation")) r.precision(*f, n.accumulation);
    if (static_cast<int>(n.accumulation) < static_cast<int>(n.precision)) {
        r.d.warning(b.loc, "accumulation " + std::string(to_string(n.accumulation)) + " is narrower than precision " +
                               std::string(to_string(n.precision)) + "; reductions may lose accuracy");
    }
}

void lower_artifact(const Block& b, Artifact& a, Reader& r) {
    check_fields(b, kArtifactKeys, r.d);
    a.loc = b.loc;
    if (const Field* f = b.find("gradient")) r.boolean(*f, a.gradient);
    if (const Field* f = b.find("iterations")) r.uint_as(*f, 0, 64, a.iterations);
    if (const Field* f = b.find("weight")) r.real_as(*f, -4.0, 16.0, a.weight);
    if (const Field* f = b.find("learning_rate")) r.real_as(*f, 1e-6, 10.0, a.learning_rate);
    if (const Field* f = b.find("target")) r.real_as(*f, 1e-3, 16.0, a.target);
    if (const Field* f = b.find("weight_min")) r.real_as(*f, -4.0, 16.0, a.weight_min);
    if (const Field* f = b.find("weight_max")) r.real_as(*f, -4.0, 16.0, a.weight_max);
    if (const Field* f = b.find("radius")) r.uint_as(*f, 1, 8, a.radius);

    if (a.weight_min > a.weight_max) {
        r.d.error(b.loc, "artifact '" + a.name + "': weight_min must be <= weight_max");
    } else if (a.weight < a.weight_min || a.weight > a.weight_max) {
        r.d.error(b.loc, "artifact '" + a.name + "': weight " + fmt(a.weight) + " lies outside [" +
                             fmt(a.weight_min) + ", " + fmt(a.weight_max) + "]");
    }
    if (a.gradient && a.iterations == 0) {
        r.d.warning(b.loc, "artifact '" + a.name + "': gradient is enabled but iterations = 0, no optimisation will run");
    }
    if (!a.gradient && (b.find("learning_rate") || b.find("target"))) {
        r.d.warning(b.loc, "artifact '" + a.name + "': learning_rate/target have no effect while gradient is disabled");
    }
}

bool apply_profile_field(const Field& f, RenderProfile& p, Reader& r) {
    const std::string& k = f.key;
    if (k == "base") return true;  // handled by the caller
    if (k == "target_fps") return r.real(f, 1.0, 1000.0, p.target_fps);
    if (k == "enhance_budget_ms") return r.real(f, 0.05, 1000.0, p.enhance_budget_ms);
    if (k == "adaptive_lod") return r.boolean(f, p.adaptive_lod);
    if (k == "lod_min") return r.lod(f, p.lod_min);
    if (k == "lod_max") return r.lod(f, p.lod_max);
    if (k == "lod_start") return r.lod(f, p.lod_start);
    if (k == "dynamic_resolution") return r.boolean(f, p.dynamic_resolution);
    if (k == "resolution_scale_min") return r.real_as(f, 0.1, 1.0, p.resolution_scale_min);
    if (k == "resolution_scale_max") return r.real_as(f, 0.1, 1.0, p.resolution_scale_max);
    if (k == "resolution_scale_step") return r.real_as(f, 0.01, 0.9, p.resolution_scale_step);
    if (k == "resolution_scale_start") return r.real_as(f, 0.1, 1.0, p.resolution_scale_start);
    if (k == "upscale_threshold") return r.real(f, 0.05, 0.99, p.upscale_threshold);
    if (k == "hysteresis_frames") return r.uint_as(f, 1, 100000, p.hysteresis_frames);
    if (k == "downscale_frames") return r.uint_as(f, 1, 100000, p.downscale_frames);
    if (k == "memory_budget_mb") return r.uint_as(f, 16, 65536, p.memory_budget_mb);
    if (k == "memory_block_mb") return r.uint_as(f, 1, 4096, p.memory_block_mb);
    if (k == "buffer_cache_mb") return r.uint_as(f, 0, 65536, p.buffer_cache_mb);
    if (k == "frames_in_flight") return r.uint_as(f, 1, 4, p.frames_in_flight);
    return false;
}

// Resolves `name` to an artifact index or kFramebuffer; reports unknown names.
bool resolve_ref(const Field& f, const std::map<std::string, std::uint32_t>& names, std::uint32_t& out, Reader& r) {
    std::string n;
    if (!r.name(f, n)) return false;
    if (n == "framebuffer") {
        out = kFramebuffer;
        return true;
    }
    auto it = names.find(n);
    if (it == names.end()) {
        r.d.error(f.value.loc, "'" + f.key + "' refers to unknown resource '" + n +
                                   "' (expected 'framebuffer' or the name of an !artifact)");
        return false;
    }
    out = it->second;
    return true;
}

}  // namespace

std::uint32_t Module::find_artifact(std::string_view name) const {
    for (std::uint32_t i = 0; i < artifacts.size(); ++i)
        if (artifacts[i].name == name) return i;
    return kFramebuffer;
}

std::string Module::resource_name(std::uint32_t ref) const {
    if (ref == kFramebuffer) return "framebuffer";
    return ref < artifacts.size() ? artifacts[ref].name : "<invalid>";
}

bool lower(const lang::Program& program, Module& m, Diagnostics& d) {
    m = Module{};
    Reader r{d};
    const std::size_t errors_before = d.error_count();

    // ---- pass 1: names ----------------------------------------------------
    std::map<std::string, std::uint32_t> artifact_names;
    std::map<std::string, const Block*> profile_blocks;
    std::vector<const Block*> artifact_blocks;
    const Block* float_block = nullptr;
    const Block* render_block = nullptr;

    for (const auto& b : program.blocks) {
        switch (b.kind) {
            case BlockKind::Float:
                if (float_block) {
                    d.error(b.loc, "only one !float block is allowed (first on line " +
                                       std::to_string(float_block->loc.line) + ")");
                } else {
                    float_block = &b;
                }
                break;
            case BlockKind::Artifact: {
                Artifact a;
                a.name = b.name.empty() ? "artifact" + std::to_string(m.artifacts.size()) : b.name;
                if (a.name == "framebuffer" || a.name == "display") {
                    d.error(b.loc, "'" + a.name + "' is a reserved resource name");
                }
                if (!artifact_names.emplace(a.name, static_cast<std::uint32_t>(m.artifacts.size())).second) {
                    d.error(b.loc, "duplicate artifact name '" + a.name + "'");
                }
                m.artifacts.push_back(std::move(a));
                artifact_blocks.push_back(&b);
                break;
            }
            case BlockKind::Render:
                if (render_block) {
                    d.error(b.loc, "only one render block is allowed (first on line " +
                                       std::to_string(render_block->loc.line) + ")");
                } else {
                    render_block = &b;
                }
                break;
            case BlockKind::Profile:
                if (b.name.empty()) {
                    d.error(b.loc, "profile blocks need a name, e.g. `profile gen11 { ... }`");
                } else if (!profile_blocks.emplace(b.name, &b).second) {
                    d.error(b.loc, "duplicate profile '" + b.name + "'");
                }
                break;
        }
    }

    // ---- pass 2: block contents --------------------------------------------
    if (float_block) lower_float(*float_block, m.numerics, r);

    for (std::size_t i = 0; i < m.artifacts.size(); ++i) {
        const Block& b = *artifact_blocks[i];
        Artifact& a = m.artifacts[i];
        lower_artifact(b, a, r);
        if (const Field* f = b.find("source")) resolve_ref(*f, artifact_names, a.source, r);
    }

    bool profile_explicit = false;
    if (!render_block) {
        d.error(lang::SourceLocation{}, "missing render block, e.g. `render { input = framebuffer; output = display; }`");
    } else {
        const Block& b = *render_block;
        check_fields(b, kRenderKeys, d);
        m.render.loc = b.loc;
        if (const Field* f = b.find("input")) resolve_ref(*f, artifact_names, m.render.input, r);
        if (const Field* f = b.find("output")) {
            std::string out;
            if (r.name(*f, out)) {
                if (out != "display") {
                    d.error(f->value.loc, "unsupported render output '" + out + "' (only 'display' is available)");
                }
                m.render.output = out;
            }
        }
        if (const Field* f = b.find("backend")) {
            std::string be;
            if (r.name(*f, be) && !parse_backend(be, m.render.backend)) {
                d.error(f->value.loc, "unknown backend '" + be + "' (expected auto|cpu|vulkan)");
            }
        }
        if (const Field* f = b.find("profile")) {
            profile_explicit = r.name(*f, m.render.profile_name);
        }
    }

    // ---- profile resolution -------------------------------------------------
    if (!profile_explicit && profile_blocks.size() == 1) {
        m.render.profile_name = profile_blocks.begin()->first;  // a single declared profile is used implicitly
    }
    {
        const std::string& pname = m.render.profile_name;
        auto it = profile_blocks.find(pname);
        RenderProfile base;
        if (it != profile_blocks.end()) {
            const Block& b = *it->second;
            check_fields(b, kProfileKeys, d);
            std::string base_name = builtin_profile(pname, base) ? pname : "balanced";
            if (const Field* f = b.find("base")) {
                if (r.name(*f, base_name) && !builtin_profile(base_name, base)) {
                    d.error(f->value.loc, "unknown base profile '" + base_name + "' (expected " +
                                              join(builtin_profile_names()) + ")");
                }
            } else if (base_name == "balanced") {
                builtin_profile("balanced", base);
            }
            base.name = pname;
            for (const auto& f : b.fields) apply_profile_field(f, base, r);
            const std::string why = validate_profile(base);
            if (!why.empty()) d.error(b.loc, "profile '" + pname + "': " + why);
        } else if (!builtin_profile(pname, base)) {
            d.error(render_block ? render_block->loc : lang::SourceLocation{},
                    "unknown profile '" + pname + "' (declare `profile " + pname + " { ... }` or use one of: " +
                        join(builtin_profile_names()) + ")");
        }
        m.profile = base;
        for (const auto& [name, block] : profile_blocks) {
            if (name != pname) d.warning(block->loc, "profile '" + name + "' is declared but not used by render");
        }
    }

    // ---- dependency graph: cycles + live set + topological order ------------
    const std::uint32_t n = static_cast<std::uint32_t>(m.artifacts.size());
    std::vector<int> color(n, 0);  // 0 = new, 1 = on stack, 2 = done
    bool cycle = false;
    std::function<void(std::uint32_t, std::vector<std::uint32_t>*)> visit = [&](std::uint32_t i,
                                                                               std::vector<std::uint32_t>* order) {
        if (color[i] == 2 || cycle) return;
        if (color[i] == 1) {
            d.error(m.artifacts[i].loc, "dependency cycle through artifact '" + m.artifacts[i].name + "'");
            cycle = true;
            return;
        }
        color[i] = 1;
        const std::uint32_t src = m.artifacts[i].source;
        if (src != kFramebuffer && src < n) visit(src, order);
        color[i] = 2;
        if (order) order->push_back(i);
    };
    for (std::uint32_t i = 0; i < n && !cycle; ++i) visit(i, nullptr);

    // Resolve which artifact's output reaches the display.
    m.render.result = m.render.input;
    if (!cycle && m.render.input == kFramebuffer && n > 0) {
        std::vector<bool> consumed(n, false);
        for (const auto& a : m.artifacts)
            if (a.source != kFramebuffer && a.source < n) consumed[a.source] = true;
        std::vector<std::uint32_t> terminals;
        for (std::uint32_t i = 0; i < n; ++i)
            if (!consumed[i]) terminals.push_back(i);
        if (!terminals.empty()) {
            m.render.result = terminals.back();
            if (terminals.size() > 1) {
                d.warning(m.render.loc, "several independent artifact chains end in the framebuffer; displaying '" +
                                            m.artifacts[m.render.result].name +
                                            "' (set `render { input = <artifact>; }` or chain them with `source`)");
            }
        }
    }

    if (!cycle && m.render.result != kFramebuffer && m.render.result < n) {
        std::fill(color.begin(), color.end(), 0);
        visit(m.render.result, &m.order);
    }
    if (!cycle) {
        for (std::uint32_t i = 0; i < n; ++i) {
            if (std::find(m.order.begin(), m.order.end(), i) == m.order.end()) {
                d.warning(m.artifacts[i].loc, "artifact '" + m.artifacts[i].name +
                                                  "' does not contribute to the render output and will not run");
            }
        }
    }

    return d.error_count() == errors_before;
}

CompileResult compile(std::string_view source) {
    CompileResult result;
    lang::ParseResult parsed = lang::parse(source);
    result.diagnostics.append(parsed.diagnostics);
    if (parsed.ok()) lower(parsed.program, result.module, result.diagnostics);
    return result;
}

}  // namespace isc::ir
