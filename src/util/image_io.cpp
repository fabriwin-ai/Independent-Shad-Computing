#include "isc/util/image_io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

namespace isc::util {
namespace {

float smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float hash01(std::uint32_t x, std::uint32_t y) {
    std::uint32_t h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

bool read_token(std::istream& in, std::string& tok) {
    tok.clear();
    char c;
    while (in.get(c)) {
        if (c == '#') {
            std::string skip;
            std::getline(in, skip);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!tok.empty()) return true;
            continue;
        }
        tok += c;
    }
    return !tok.empty();
}

}  // namespace

bool write_ppm(const std::string& path, const RenderTarget& img, std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot open '" + path + "' for writing";
        return false;
    }
    out << "P6\n" << img.width() << ' ' << img.height() << "\n255\n";
    std::vector<unsigned char> row(static_cast<std::size_t>(img.width()) * 3);
    for (std::uint32_t y = 0; y < img.height(); ++y) {
        const float* p = img.row(y);
        for (std::uint32_t x = 0; x < img.width(); ++x)
            for (int c = 0; c < 3; ++c)
                row[x * 3 + c] = static_cast<unsigned char>(std::lround(std::clamp(p[x * 4 + c], 0.0f, 1.0f) * 255.0f));
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(out);
}

bool read_ppm(const std::string& path, RenderTarget& img, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open '" + path + "'";
        return false;
    }
    std::string magic, ws, hs, ms;
    if (!read_token(in, magic) || magic != "P6" || !read_token(in, ws) || !read_token(in, hs) || !read_token(in, ms)) {
        if (error) *error = "'" + path + "' is not a binary PPM (P6)";
        return false;
    }
    const int w = std::atoi(ws.c_str()), h = std::atoi(hs.c_str()), maxval = std::atoi(ms.c_str());
    if (w <= 0 || h <= 0 || maxval <= 0 || maxval > 255) {
        if (error) *error = "unsupported PPM header in '" + path + "' (8-bit only)";
        return false;
    }
    img.resize(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    std::vector<unsigned char> row(static_cast<std::size_t>(w) * 3);
    for (int y = 0; y < h; ++y) {
        in.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()));
        if (!in) {
            if (error) *error = "truncated PPM '" + path + "'";
            return false;
        }
        float* p = img.row(static_cast<std::uint32_t>(y));
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < 3; ++c) p[x * 4 + c] = static_cast<float>(row[x * 3 + c]) / static_cast<float>(maxval);
            p[x * 4 + 3] = 1.0f;
        }
    }
    return true;
}

void fill_test_pattern(RenderTarget& img, std::uint32_t frame) {
    const std::uint32_t W = img.width(), H = img.height();
    if (W == 0 || H == 0) return;
    const float t = static_cast<float>(frame);
    for (std::uint32_t y = 0; y < H; ++y) {
        float* row = img.row(y);
        for (std::uint32_t x = 0; x < W; ++x) {
            const float u = W > 1 ? static_cast<float>(x) / static_cast<float>(W - 1) : 0.0f;
            const float v = H > 1 ? static_cast<float>(y) / static_cast<float>(H - 1) : 0.0f;
            float r = 0.20f + 0.50f * u;
            float g = 0.25f + 0.40f * v;
            float b = 0.35f + 0.15f * std::sin(6.2831853f * (u + v) + t * 0.05f);

            if (u < 0.5f && v < 0.5f) {  // low-contrast checker, scrolls with time
                const std::uint32_t cx = (x + frame) / 16u, cy = y / 16u;
                const float k = ((cx + cy) & 1u) ? 0.10f : -0.10f;
                r += k;
                g += k;
                b += k;
            }
            const float du = u - (0.70f + 0.05f * std::sin(t * 0.03f)), dv = v - 0.60f;
            const float disc = smoothstep(0.22f, 0.18f, std::sqrt(du * du + dv * dv));  // soft disc
            r += 0.30f * disc;
            b -= 0.10f * disc;
            if (u < 0.45f && v > 0.55f) {  // fine stripes
                const float s = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 0.9f);
                r += 0.08f * (s - 0.5f);
                g += 0.08f * (s - 0.5f);
                b += 0.08f * (s - 0.5f);
            }
            const float nz = (hash01(x, y) - 0.5f) * 0.02f;
            row[x * 4 + 0] = std::clamp(r + nz, 0.0f, 1.0f);
            row[x * 4 + 1] = std::clamp(g + nz, 0.0f, 1.0f);
            row[x * 4 + 2] = std::clamp(b + nz, 0.0f, 1.0f);
            row[x * 4 + 3] = 1.0f;
        }
    }
}

}  // namespace isc::util
