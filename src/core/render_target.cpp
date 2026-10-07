#include "isc/core/render_target.hpp"

#include <algorithm>
#include <cmath>

namespace isc {

void RenderTarget::resize(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width) * height * kChannels, 0.0f);
}

void RenderTarget::fill(float r, float g, float b, float a) {
    for (std::size_t i = 0; i < pixels_.size(); i += kChannels) {
        pixels_[i + 0] = r;
        pixels_[i + 1] = g;
        pixels_[i + 2] = b;
        pixels_[i + 3] = a;
    }
}

double max_abs_diff(const RenderTarget& a, const RenderTarget& b) {
    if (a.width() != b.width() || a.height() != b.height()) return INFINITY;
    double m = 0.0;
    const std::size_t n = a.pixel_count() * RenderTarget::kChannels;
    for (std::size_t i = 0; i < n; ++i) {
        m = std::max(m, static_cast<double>(std::fabs(a.data()[i] - b.data()[i])));
    }
    return m;
}

double mean_abs_diff(const RenderTarget& a, const RenderTarget& b) {
    if (a.width() != b.width() || a.height() != b.height()) return INFINITY;
    const std::size_t n = a.pixel_count() * RenderTarget::kChannels;
    if (n == 0) return 0.0;
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += std::fabs(a.data()[i] - b.data()[i]);
    return s / static_cast<double>(n);
}

}  // namespace isc
