#pragma once

// CPU reference kernels. The GPU shaders in shaders/*.comp implement the
// same formulas in the same order so results match within tolerance.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "isc/core/render_target.hpp"
#include "isc/opt/optimizer.hpp"
#include "isc/sched/scheduler.hpp"

namespace isc::cpu {

constexpr std::uint32_t kRowGrain = 8;

// Bilinear resample with pixel-centre mapping and clamp-to-edge.
template <class T>
void resample(sched::Scheduler& s, const RenderTarget& src, RenderTarget& dst) {
    const std::uint32_t sw = src.width(), sh = src.height(), dw = dst.width(), dh = dst.height();
    const T rx = static_cast<T>(sw) / static_cast<T>(dw);
    const T ry = static_cast<T>(sh) / static_cast<T>(dh);
    s.parallel_for(dh, kRowGrain, [&](std::uint32_t y0r, std::uint32_t y1r, std::uint32_t) {
        for (std::uint32_t y = y0r; y < y1r; ++y) {
            const T fy = std::clamp((static_cast<T>(y) + T(0.5)) * ry - T(0.5), T(0), static_cast<T>(sh - 1));
            const auto y0 = static_cast<std::uint32_t>(std::floor(fy));
            const std::uint32_t y1 = std::min(y0 + 1, sh - 1);
            const T ty = fy - static_cast<T>(y0);
            float* out = dst.row(y);
            for (std::uint32_t x = 0; x < dw; ++x) {
                const T fx = std::clamp((static_cast<T>(x) + T(0.5)) * rx - T(0.5), T(0), static_cast<T>(sw - 1));
                const auto x0 = static_cast<std::uint32_t>(std::floor(fx));
                const std::uint32_t x1 = std::min(x0 + 1, sw - 1);
                const T tx = fx - static_cast<T>(x0);
                const float* p00 = src.pixel(x0, y0);
                const float* p10 = src.pixel(x1, y0);
                const float* p01 = src.pixel(x0, y1);
                const float* p11 = src.pixel(x1, y1);
                for (int c = 0; c < 4; ++c) {
                    const T a = static_cast<T>(p00[c]) * (T(1) - tx) + static_cast<T>(p10[c]) * tx;
                    const T b = static_cast<T>(p01[c]) * (T(1) - tx) + static_cast<T>(p11[c]) * tx;
                    out[x * 4 + c] = static_cast<float>(a * (T(1) - ty) + b * ty);
                }
            }
        }
    });
}

// Horizontal box blur, radius r, clamp-to-edge.
template <class T>
void blur_h(sched::Scheduler& s, const RenderTarget& src, RenderTarget& tmp, std::uint32_t radius) {
    const int w = static_cast<int>(src.width());
    const int r = static_cast<int>(radius);
    const T inv = T(1) / static_cast<T>(2 * r + 1);
    s.parallel_for(src.height(), kRowGrain, [&](std::uint32_t y0, std::uint32_t y1, std::uint32_t) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            const float* in = src.row(y);
            float* out = tmp.row(y);
            for (int x = 0; x < w; ++x) {
                T acc[4] = {T(0), T(0), T(0), T(0)};
                for (int k = -r; k <= r; ++k) {
                    const int xx = std::clamp(x + k, 0, w - 1);
                    for (int c = 0; c < 4; ++c) acc[c] += static_cast<T>(in[xx * 4 + c]);
                }
                for (int c = 0; c < 4; ++c) out[x * 4 + c] = static_cast<float>(acc[c] * inv);
            }
        }
    });
}

// Vertical box blur of `tmp`, then detail = src - blur (rgb), alpha = 0.
template <class T>
void detail(sched::Scheduler& s, const RenderTarget& src, const RenderTarget& tmp, RenderTarget& out,
            std::uint32_t radius) {
    const int w = static_cast<int>(src.width());
    const int h = static_cast<int>(src.height());
    const int r = static_cast<int>(radius);
    const T inv = T(1) / static_cast<T>(2 * r + 1);
    s.parallel_for(src.height(), kRowGrain, [&](std::uint32_t y0, std::uint32_t y1, std::uint32_t) {
        for (std::uint32_t yu = y0; yu < y1; ++yu) {
            const int y = static_cast<int>(yu);
            const float* in = src.row(yu);
            float* d = out.row(yu);
            for (int x = 0; x < w; ++x) {
                T acc[3] = {T(0), T(0), T(0)};
                for (int k = -r; k <= r; ++k) {
                    const float* t = tmp.row(static_cast<std::uint32_t>(std::clamp(y + k, 0, h - 1)));
                    for (int c = 0; c < 3; ++c) acc[c] += static_cast<T>(t[x * 4 + c]);
                }
                for (int c = 0; c < 3; ++c)
                    d[x * 4 + c] = static_cast<float>(static_cast<T>(in[x * 4 + c]) - acc[c] * inv);
                d[x * 4 + 3] = 0.0f;
            }
        }
    });
}

// Per-row partial sums, then a serial sum in row order: deterministic for
// any thread count.
template <class Acc>
double edge_energy_a(sched::Scheduler& s, const RenderTarget& src) {
    const std::uint32_t w = src.width(), h = src.height();
    std::vector<Acc> rows(h, Acc(0));
    s.parallel_for(h, kRowGrain, [&](std::uint32_t y0, std::uint32_t y1, std::uint32_t) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            const std::uint32_t yn = std::min(y + 1, h - 1);
            Acc a = Acc(0);
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t xn = std::min(x + 1, w - 1);
                const float* p = src.pixel(x, y);
                const float* px = src.pixel(xn, y);
                const float* py = src.pixel(x, yn);
                for (int c = 0; c < 3; ++c) {
                    const Acc gx = static_cast<Acc>(px[c]) - static_cast<Acc>(p[c]);
                    const Acc gy = static_cast<Acc>(py[c]) - static_cast<Acc>(p[c]);
                    a += gx * gx + gy * gy;
                }
            }
            rows[y] = a;
        }
    });
    Acc sum = Acc(0);
    for (Acc v : rows) sum += v;
    const double n = static_cast<double>(w) * h * 3.0;
    return n > 0 ? static_cast<double>(sum) / n : 0.0;
}

template <class Acc>
void edge_energy_bc(sched::Scheduler& s, const RenderTarget& src, const RenderTarget& det, double& B, double& C) {
    const std::uint32_t w = src.width(), h = src.height();
    std::vector<Acc> rb(h, Acc(0)), rc(h, Acc(0));
    s.parallel_for(h, kRowGrain, [&](std::uint32_t y0, std::uint32_t y1, std::uint32_t) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            const std::uint32_t yn = std::min(y + 1, h - 1);
            Acc b = Acc(0), c2 = Acc(0);
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t xn = std::min(x + 1, w - 1);
                const float* p = src.pixel(x, y);
                const float* px = src.pixel(xn, y);
                const float* py = src.pixel(x, yn);
                const float* d = det.pixel(x, y);
                const float* dx = det.pixel(xn, y);
                const float* dy = det.pixel(x, yn);
                for (int c = 0; c < 3; ++c) {
                    const Acc gxi = static_cast<Acc>(px[c]) - static_cast<Acc>(p[c]);
                    const Acc gyi = static_cast<Acc>(py[c]) - static_cast<Acc>(p[c]);
                    const Acc gxd = static_cast<Acc>(dx[c]) - static_cast<Acc>(d[c]);
                    const Acc gyd = static_cast<Acc>(dy[c]) - static_cast<Acc>(d[c]);
                    b += gxi * gxd + gyi * gyd;
                    c2 += gxd * gxd + gyd * gyd;
                }
            }
            rb[y] = b;
            rc[y] = c2;
        }
    });
    Acc sb = Acc(0), sc = Acc(0);
    for (std::uint32_t y = 0; y < h; ++y) {
        sb += rb[y];
        sc += rc[y];
    }
    const double n = static_cast<double>(w) * h * 3.0;
    B = n > 0 ? static_cast<double>(sb) / n : 0.0;
    C = n > 0 ? static_cast<double>(sc) / n : 0.0;
}

// out = clamp(src + weight * upsample(detail), 0, 1); alpha from src.
template <class T>
void composite(sched::Scheduler& s, const RenderTarget& src, const RenderTarget& det, float weight, RenderTarget& out) {
    const std::uint32_t w = src.width(), h = src.height(), dw = det.width(), dh = det.height();
    const bool same = (w == dw && h == dh);
    const T rx = static_cast<T>(dw) / static_cast<T>(w);
    const T ry = static_cast<T>(dh) / static_cast<T>(h);
    const T wt = static_cast<T>(weight);
    s.parallel_for(h, kRowGrain, [&](std::uint32_t y0r, std::uint32_t y1r, std::uint32_t) {
        for (std::uint32_t y = y0r; y < y1r; ++y) {
            const float* in = src.row(y);
            float* o = out.row(y);
            T fy = 0, ty = 0;
            std::uint32_t y0 = 0, y1 = 0;
            if (!same) {
                fy = std::clamp((static_cast<T>(y) + T(0.5)) * ry - T(0.5), T(0), static_cast<T>(dh - 1));
                y0 = static_cast<std::uint32_t>(std::floor(fy));
                y1 = std::min(y0 + 1, dh - 1);
                ty = fy - static_cast<T>(y0);
            }
            for (std::uint32_t x = 0; x < w; ++x) {
                T d[3];
                if (same) {
                    const float* p = det.pixel(x, y);
                    for (int c = 0; c < 3; ++c) d[c] = static_cast<T>(p[c]);
                } else {
                    const T fx = std::clamp((static_cast<T>(x) + T(0.5)) * rx - T(0.5), T(0), static_cast<T>(dw - 1));
                    const auto x0 = static_cast<std::uint32_t>(std::floor(fx));
                    const std::uint32_t x1 = std::min(x0 + 1, dw - 1);
                    const T tx = fx - static_cast<T>(x0);
                    const float* p00 = det.pixel(x0, y0);
                    const float* p10 = det.pixel(x1, y0);
                    const float* p01 = det.pixel(x0, y1);
                    const float* p11 = det.pixel(x1, y1);
                    for (int c = 0; c < 3; ++c) {
                        const T a = static_cast<T>(p00[c]) * (T(1) - tx) + static_cast<T>(p10[c]) * tx;
                        const T b = static_cast<T>(p01[c]) * (T(1) - tx) + static_cast<T>(p11[c]) * tx;
                        d[c] = a * (T(1) - ty) + b * ty;
                    }
                }
                for (int c = 0; c < 3; ++c)
                    o[x * 4 + c] = static_cast<float>(std::clamp(static_cast<T>(in[x * 4 + c]) + wt * d[c], T(0), T(1)));
                o[x * 4 + 3] = in[x * 4 + 3];
            }
        }
    });
}

}  // namespace isc::cpu
