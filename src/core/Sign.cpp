#include "core/Sign.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lookit::core {

namespace sign {

Rect move(const Rect& start, double dx, double dy, double pw, double ph) {
    Rect r = start;
    r.x = std::clamp(start.x + dx, 0.0, std::max(0.0, pw - start.w));
    r.y = std::clamp(start.y + dy, 0.0, std::max(0.0, ph - start.h));
    return r;
}

Rect resize(const Rect& start, double dx, double dy, double pw, double ph, double min_h) {
    if (start.w <= 0 || start.h <= 0) return start;
    const double aspect = start.w / start.h;
    // The corner follows whichever way the pointer went further, in proportion.
    double k = std::max((start.w + dx) / start.w, (start.h + dy) / start.h);
    const double kmin = std::min(1.0, min_h / start.h);
    const double kmax = std::max(kmin, std::min((pw - start.x) / start.w, (ph - start.y) / start.h));
    k = std::clamp(k, kmin, kmax);
    Rect r = start;
    r.h = start.h * k;
    r.w = r.h * aspect;
    return r;
}

Rect resize_free(const Rect& start, double dx, double dy, double pw, double ph, double min) {
    Rect r = start;
    r.w = std::clamp(start.w + dx, min, std::max(min, pw - start.x));
    r.h = std::clamp(start.h + dy, min, std::max(min, ph - start.y));
    return r;
}

std::pair<double, double> clamp_point(double x, double y, double pw, double ph) {
    return {std::clamp(x, 0.0, pw), std::clamp(y, 0.0, ph)};
}

void ink(std::uint8_t* px, int w, int h, int stride) {
    // Lightness above `paper` is paper (clear); below `full` is solid ink; a
    // straight ramp between keeps antialiased edges smooth.
    constexpr int paper = 215, full = 120;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = px + std::size_t(y) * std::size_t(stride);
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = row + 4 * x;
            if (p[3] == 0) continue;
            const int lum = (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000;
            int a = lum >= paper ? 0 : lum <= full ? 255 : (paper - lum) * 255 / (paper - full);
            a = a * p[3] / 255;
            if (a == 0) { std::memset(p, 0, 4); continue; }
            // Darken the ink a touch where it was faint, so a light scan still reads.
            if (lum > full) {
                const int k = 255 * full / std::max(lum, 1);
                for (int c = 0; c < 3; ++c) p[c] = std::uint8_t(p[c] * k / 255);
            }
            p[3] = std::uint8_t(a);
        }
    }
}

Rect ink_bounds(const std::uint8_t* px, int w, int h, int stride, int threshold) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = px + std::size_t(y) * std::size_t(stride);
        for (int x = 0; x < w; ++x)
            if (row[4 * x + 3] > threshold) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
    }
    if (x1 < 0) return {};
    return {double(x0), double(y0), double(x1 - x0 + 1), double(y1 - y0 + 1)};
}

}  // namespace sign

namespace convert {

void argb32_to_rgba(const std::uint8_t* src, int src_stride, int w, int h, std::uint8_t* dst, int dst_stride,
                    bool flatten) {
    for (int y = 0; y < h; ++y) {
        const auto* s = reinterpret_cast<const std::uint32_t*>(src + std::size_t(y) * std::size_t(src_stride));
        std::uint8_t* d = dst + std::size_t(y) * std::size_t(dst_stride);
        for (int x = 0; x < w; ++x) {
            const std::uint32_t v = s[x];
            int a = int(v >> 24), r = int((v >> 16) & 0xff), g = int((v >> 8) & 0xff), b = int(v & 0xff);
            if (flatten) {
                // Premultiplied over white: c + (1 - a) * 255.
                r += 255 - a;
                g += 255 - a;
                b += 255 - a;
                a = 255;
            } else if (a > 0 && a < 255) {
                r = std::min(255, (r * 255 + a / 2) / a);
                g = std::min(255, (g * 255 + a / 2) / a);
                b = std::min(255, (b * 255 + a / 2) / a);
            } else if (a == 0) {
                r = g = b = 0;
            }
            d[4 * x + 0] = std::uint8_t(r);
            d[4 * x + 1] = std::uint8_t(g);
            d[4 * x + 2] = std::uint8_t(b);
            d[4 * x + 3] = std::uint8_t(a);
        }
    }
}

std::string page_name(const std::string& stem, int page, int count, const std::string& ext) {
    if (count <= 1) return stem + "." + ext;
    const int width = std::max(2, int(std::to_string(count).size()));
    std::string n = std::to_string(page + 1);
    if (int(n.size()) < width) n.insert(0, std::size_t(width) - n.size(), '0');
    return stem + "-p" + n + "." + ext;
}

std::string unique_name(const std::string& name, const std::function<bool(const std::string&)>& exists) {
    if (!exists(name)) return name;
    const auto slash = name.find_last_of('/');
    const auto dot = name.find_last_of('.');
    const bool has_ext = dot != std::string::npos && (slash == std::string::npos || dot > slash + 1);
    const std::string stem = has_ext ? name.substr(0, dot) : name;
    const std::string ext = has_ext ? name.substr(dot) : "";
    for (int i = 2;; ++i) {
        std::string n = stem + " (" + std::to_string(i) + ")" + ext;
        if (!exists(n)) return n;
    }
}

std::pair<int, int> pixels(double w_pt, double h_pt, int dpi) {
    const double k = dpi / 72.0;
    return {std::max(1, int(std::lround(w_pt * k))), std::max(1, int(std::lround(h_pt * k)))};
}

}  // namespace convert

}  // namespace lookit::core
