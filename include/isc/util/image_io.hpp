#pragma once

#include <cstdint>
#include <string>

#include "isc/core/render_target.hpp"

namespace isc::util {

// Binary PPM (P6, 8-bit). Values are clamped to [0,1]; no colour transform.
bool write_ppm(const std::string& path, const RenderTarget& image, std::string* error = nullptr);
bool read_ppm(const std::string& path, RenderTarget& image, std::string* error = nullptr);

// Deterministic synthetic frame (soft shapes, low-contrast checker, fine
// stripes, light noise) so the enhancement has something to work on.
// `frame` animates the pattern for multi-frame runs.
void fill_test_pattern(RenderTarget& image, std::uint32_t frame = 0);

}  // namespace isc::util
