#include "core/Search.hpp"

#include <algorithm>
#include <cmath>

namespace lookit::core::search {

layout::Rect from_pdf(double x1, double y1, double x2, double y2, double page_h) {
    const double lx = std::min(x1, x2), hx = std::max(x1, x2);
    const double ly = std::min(y1, y2), hy = std::max(y1, y2);
    return {lx, page_h - hy, hx - lx, hy - ly};
}

layout::Rect turn_rect(const layout::Rect& r, double w, double h, int turn) {
    switch (((turn % 4) + 4) % 4) {
        case 1:  return {h - (r.y + r.h), r.x, r.h, r.w};
        case 2:  return {w - (r.x + r.w), h - (r.y + r.h), r.w, r.h};
        case 3:  return {r.y, w - (r.x + r.w), r.h, r.w};
        default: return r;
    }
}

void sort(std::vector<Hit>& hits) {
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.page != b.page) return a.page < b.page;
        // Same line (tops within half a line): left to right.
        if (std::abs(a.r.y - b.r.y) > std::min(a.r.h, b.r.h) / 2) return a.r.y < b.r.y;
        return a.r.x < b.r.x;
    });
}

int step(const std::vector<Hit>& hits, int cur, int dir) {
    const int n = int(hits.size());
    if (n == 0) return -1;
    if (cur < 0 || cur >= n) return dir < 0 ? n - 1 : 0;
    return ((cur + (dir < 0 ? -1 : 1)) % n + n) % n;
}

int first_from(const std::vector<Hit>& hits, int page) {
    if (hits.empty()) return -1;
    for (int i = 0; i < int(hits.size()); ++i)
        if (hits[std::size_t(i)].page >= page) return i;
    return 0;
}

}  // namespace lookit::core::search
