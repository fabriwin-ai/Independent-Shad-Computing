#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "isc/core/types.hpp"

namespace isc {

// CPU-side RGBA32F image (interleaved, row-major, linear colour, [0,1]).
// This is the hand-off format between the host engine and the layer in the
// reference path; GPU backends import/export it into their own buffers.
class RenderTarget {
public:
    static constexpr std::uint32_t kChannels = 4;

    RenderTarget() = default;
    RenderTarget(std::uint32_t width, std::uint32_t height) { resize(width, height); }

    void resize(std::uint32_t width, std::uint32_t height);
    void fill(float r, float g, float b, float a);

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    bool empty() const { return width_ == 0 || height_ == 0; }
    std::size_t pixel_count() const { return static_cast<std::size_t>(width_) * height_; }
    std::size_t byte_size() const { return pixels_.size() * sizeof(float); }

    float* data() { return pixels_.data(); }
    const float* data() const { return pixels_.data(); }
    float* row(std::uint32_t y) { return pixels_.data() + static_cast<std::size_t>(y) * width_ * kChannels; }
    const float* row(std::uint32_t y) const {
        return pixels_.data() + static_cast<std::size_t>(y) * width_ * kChannels;
    }
    float* pixel(std::uint32_t x, std::uint32_t y) { return row(y) + static_cast<std::size_t>(x) * kChannels; }
    const float* pixel(std::uint32_t x, std::uint32_t y) const {
        return row(y) + static_cast<std::size_t>(x) * kChannels;
    }

    Ownership ownership() const { return ownership_; }
    void set_ownership(Ownership o) { ownership_ = o; }

    static std::uint64_t bytes_for(std::uint32_t width, std::uint32_t height) {
        return static_cast<std::uint64_t>(width) * height * kChannels * sizeof(float);
    }

private:
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<float> pixels_;
    Ownership ownership_ = Ownership::Host;
};

// Image comparison helpers (RGB + alpha).
double max_abs_diff(const RenderTarget& a, const RenderTarget& b);
double mean_abs_diff(const RenderTarget& a, const RenderTarget& b);

}  // namespace isc
