#include "core/Layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace lookit::core::layout {

namespace {
constexpr std::array<double, 15> kSteps = {0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0, 1.25,
                                           1.5, 2.0, 2.5, 3.0, 4.0, 6.0, 8.0};
constexpr double kEps = 1e-6;
}  // namespace

double clamp_scale(double s) {
    if (!(s > 0)) return 1.0;
    return std::clamp(s, kMinScale, kMaxScale);
}

std::vector<Row> rows(int count, Mode mode) {
    std::vector<Row> out;
    if (count <= 0) return out;
    if (mode == Mode::Continuous || count == 1) {
        for (int i = 0; i < count; ++i) out.push_back({i, -1});
        return out;
    }
    out.push_back({-1, 0});                          // the cover, on the right
    for (int i = 1; i < count; i += 2) out.push_back({i, i + 1 < count ? i + 1 : -1});
    return out;
}

int row_of(int page, Mode mode) {
    if (page <= 0 || mode == Mode::Continuous) return std::max(page, 0);
    return (page + 1) / 2;
}

int first_of_row(int row, Mode mode) {
    if (row <= 0 || mode == Mode::Continuous) return std::max(row, 0);
    return row * 2 - 1;
}

int step(int page, int dir, int count, Mode mode) {
    if (count <= 0) return -1;
    if (mode == Mode::Book && count == 1) mode = Mode::Continuous;
    const int nrows = int(rows(count, mode).size());
    const int r = std::clamp(row_of(std::clamp(page, 0, count - 1), mode) + (dir > 0 ? 1 : dir < 0 ? -1 : 0),
                             0, nrows - 1);
    return std::clamp(first_of_row(r, mode), 0, count - 1);
}

namespace {
// Width and height of a row at scale 1 (pages side by side across the spine).
Size row_size(const std::vector<Size>& p, const Row& r, Mode mode) {
    if (mode == Mode::Continuous || (r.left < 0) != (r.right < 0)) {
        // One page. In Book mode it still sits on its side of the spine, so
        // its row is as wide as a spread of two of it.
        const Size& s = p[std::size_t(r.left >= 0 ? r.left : r.right)];
        return mode == Mode::Book ? Size{2 * s.w, s.h} : s;
    }
    const Size& a = p[std::size_t(r.left)];
    const Size& b = p[std::size_t(r.right)];
    return {a.w + b.w, std::max(a.h, b.h)};
}
}  // namespace

Placed place(const std::vector<Size>& pages, double scale, double viewport_w, Mode mode,
             double gap, double margin) {
    Placed out;
    if (mode == Mode::Book && pages.size() == 1) mode = Mode::Continuous;
    const auto rs = rows(int(pages.size()), mode);
    const double spine = mode == Mode::Book ? kSpine : 0;
    double maxw = 0;
    for (const auto& r : rs) maxw = std::max(maxw, row_size(pages, r, mode).w * scale + spine);
    out.width = std::max(viewport_w, maxw + 2 * margin);
    out.pages.assign(pages.size(), Rect{});
    const double mid = std::floor(out.width / 2);
    double y = margin;
    for (const auto& r : rs) {
        const double rh = row_size(pages, r, mode).h * scale;
        auto put = [&](int i, double x) {
            const auto& s = pages[std::size_t(i)];
            const double w = s.w * scale, h = s.h * scale;
            // Pages of different heights in one spread: centred on the row.
            out.pages[std::size_t(i)] = {x, y + std::floor((rh - h) / 2), w, h};
        };
        if (mode == Mode::Continuous) {
            const double w = pages[std::size_t(r.left)].w * scale;
            put(r.left, std::floor((out.width - w) / 2));
        } else {
            if (r.left >= 0) put(r.left, mid - spine / 2 - pages[std::size_t(r.left)].w * scale);
            if (r.right >= 0) put(r.right, mid + spine / 2);
        }
        y += rh + gap;
    }
    out.height = rs.empty() ? 0 : y - gap + margin;
    return out;
}

Placed place_row(const std::vector<Size>& pages, int page, double scale, double vw, double vh,
                 Mode mode, double margin) {
    Placed out;
    out.pages.assign(pages.size(), Rect{0, -1e9, 0, 0});
    if (pages.empty()) return out;
    if (mode == Mode::Book && pages.size() == 1) mode = Mode::Continuous;
    page = std::clamp(page, 0, int(pages.size()) - 1);
    const auto rs = rows(int(pages.size()), mode);
    const Row& r = rs[std::size_t(row_of(page, mode))];
    const Size rsz = row_size(pages, r, mode);
    const double spine = mode == Mode::Book ? kSpine : 0;
    const double rw = rsz.w * scale + spine, rh = rsz.h * scale;
    out.width = std::max(vw, rw + 2 * margin);
    out.height = std::max(vh, rh + 2 * margin);
    const double y = std::floor((out.height - rh) / 2);   // centred; = margin when zoomed past
    const double mid = std::floor(out.width / 2);
    auto put = [&](int i, double x) {
        const auto& s = pages[std::size_t(i)];
        out.pages[std::size_t(i)] = {x, y + std::floor((rh - s.h * scale) / 2), s.w * scale, s.h * scale};
    };
    if (mode == Mode::Continuous) put(r.left, std::floor((out.width - pages[std::size_t(r.left)].w * scale) / 2));
    else {
        if (r.left >= 0) put(r.left, mid - spine / 2 - pages[std::size_t(r.left)].w * scale);
        if (r.right >= 0) put(r.right, mid + spine / 2);
    }
    return out;
}

std::pair<int, int> visible(const Placed& p, double y0, double y1) {
    int first = -1, last = -1;
    if (y1 <= y0) return {-1, -1};
    for (int i = 0; i < int(p.pages.size()); ++i) {
        const auto& r = p.pages[std::size_t(i)];
        if (r.y + r.h > y0 && r.y < y1) {
            if (first < 0) first = i;
            last = i;
        } else if (first >= 0 && r.y >= y1) {
            break;
        }
    }
    return {first, last};
}

int page_at(const Placed& p, double y) {
    const auto& v = p.pages;
    if (v.empty()) return -1;
    int best = 0;
    double bestd = 1e300;
    for (int i = 0; i < int(v.size()); ++i) {
        const auto& r = v[i];
        if (y >= r.y && y < r.y + r.h) return i;
        const double d = y < r.y ? r.y - y : y - (r.y + r.h);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

double fit_width(const std::vector<Size>& pages, double vw, Mode mode, double margin) {
    if (mode == Mode::Book && pages.size() == 1) mode = Mode::Continuous;
    double maxw = 0;
    for (const auto& r : rows(int(pages.size()), mode)) maxw = std::max(maxw, row_size(pages, r, mode).w);
    const double spine = mode == Mode::Book ? kSpine : 0;
    if (maxw <= 0 || vw <= 2 * margin + spine) return 1.0;
    return clamp_scale((vw - 2 * margin - spine) / maxw);
}

double fit_page(const std::vector<Size>& pages, int i, double vw, double vh, Mode mode, double margin) {
    if (i < 0 || i >= int(pages.size())) return 1.0;
    if (mode == Mode::Book && pages.size() == 1) mode = Mode::Continuous;
    const auto rs = rows(int(pages.size()), mode);
    const Size s = row_size(pages, rs[std::size_t(row_of(i, mode))], mode);
    const double spine = mode == Mode::Book ? kSpine : 0;
    if (s.w <= 0 || s.h <= 0 || vw <= 2 * margin + spine || vh <= 2 * margin) return 1.0;
    return clamp_scale(std::min((vw - 2 * margin - spine) / s.w, (vh - 2 * margin) / s.h));
}

double zoom_in(double s) {
    for (double z : kSteps)
        if (z > s + kEps) return z;
    return kSteps.back();
}

double zoom_out(double s) {
    for (auto it = kSteps.rbegin(); it != kSteps.rend(); ++it)
        if (*it < s - kEps) return *it;
    return kSteps.front();
}

}  // namespace lookit::core::layout
