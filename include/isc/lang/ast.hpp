#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "isc/lang/diagnostics.hpp"

namespace isc::lang {

enum class BlockKind { Float, Artifact, Render, Profile };

std::string_view to_string(BlockKind k);

struct Value {
    enum class Kind { Identifier, Integer, Float, String };
    Kind kind = Kind::Identifier;
    std::string text;     // identifier / string contents / numeric spelling
    double number = 0.0;  // Integer and Float
    SourceLocation loc;

    bool is_number() const { return kind == Kind::Integer || kind == Kind::Float; }
};

struct Field {
    std::string key;
    Value value;
    SourceLocation loc;
};

struct Block {
    BlockKind kind = BlockKind::Artifact;
    std::string name;  // optional: `!artifact sharpen { ... }`, `profile gen11 { ... }`
    SourceLocation loc;
    std::vector<Field> fields;

    const Field* find(std::string_view key) const {
        for (const auto& f : fields)
            if (f.key == key) return &f;
        return nullptr;
    }
};

struct Program {
    std::vector<Block> blocks;
};

std::string dump(const Program& program);

}  // namespace isc::lang
