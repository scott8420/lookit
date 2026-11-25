#include "core/Marks.hpp"

#include <algorithm>
#include <cmath>

namespace lookit::core::marks {

namespace {
int norm(int t) { return ((t % 4) + 4) % 4; }
// The seen page's sides.
std::pair<double, double> seen(double w, double h, int t) { return norm(t) % 2 ? std::pair{h, w} : std::pair{w, h}; }
}  // namespace

const char* noun(MarkKind k) {
    switch (k) {
        case MarkKind::Signature: return "signature";
        case MarkKind::Highlight: return "highlight";
        case MarkKind::Note:      return "note";
        case MarkKind::Box:       return "box";
        case MarkKind::Ellipse:   return "ellipse";
        case MarkKind::Line:      return "line";
        case MarkKind::Arrow:     return "arrow";
        case MarkKind::Text:      return "text";
    }
    return "mark";
}

bool is_line(MarkKind k) { return k == MarkKind::Line || k == MarkKind::Arrow; }
bool resizable(MarkKind k) { return k != MarkKind::Highlight && k != MarkKind::Note; }
bool upright(MarkKind k) { return k == MarkKind::Signature || k == MarkKind::Text || k == MarkKind::Note; }

const std::vector<std::uint32_t>& palette() {
    static const std::vector<std::uint32_t> p{
        0xffd400ffu,   // yellow
        0xe01b24ffu,   // red
        0x1c71d8ffu,   // blue
        0x2ec27effu,   // green
        0x9141acffu,   // purple
        0x000000ffu,   // black
    };
    return p;
}

std::uint32_t default_color(MarkKind k) {
    switch (k) {
        case MarkKind::Highlight:
        case MarkKind::Note:      return 0xffd400ffu;
        case MarkKind::Text:
        case MarkKind::Signature: return 0x000000ffu;
        default:                  return 0xe01b24ffu;
    }
}

Point turn_point(Point p, double w, double h, int t) {
    switch (norm(t)) {
        case 1:  return {h - p.y, p.x};
        case 2:  return {w - p.x, h - p.y};
        case 3:  return {p.y, w - p.x};
        default: return p;
    }
}

Point unturn_point(Point p, double w, double h, int t) {
    const auto [sw, sh] = seen(w, h, t);
    return turn_point(p, sw, sh, norm(4 - norm(t)));
}

Point unturn_vector(Point v, int t) {
    // Turning back: the inverse rotation of (x, y) -> (-y, x).
    switch (norm(t)) {
        case 1:  return {v.y, -v.x};
        case 2:  return {-v.x, -v.y};
        case 3:  return {-v.y, v.x};
        default: return v;
    }
}

Rect span(Point a, Point b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

Rect turn_rect(const Rect& r, double w, double h, int t) {
    return span(turn_point({r.x, r.y}, w, h, t), turn_point({r.x + r.w, r.y + r.h}, w, h, t));
}

Rect unturn_rect(const Rect& r, double w, double h, int t) {
    return span(unturn_point({r.x, r.y}, w, h, t), unturn_point({r.x + r.w, r.y + r.h}, w, h, t));
}

std::pair<Point, Point> view_line(const Mark& m, double w, double h, int t) {
    return {turn_point({m.x, m.y}, w, h, t), turn_point({m.x + m.w, m.y + m.h}, w, h, t)};
}

Rect view_box(const Mark& m, double w, double h, int t) {
    if (is_line(m.kind)) {
        const auto [a, b] = view_line(m, w, h, t);
        return span(a, b);
    }
    return turn_rect({m.x, m.y, m.w, m.h}, w, h, t);
}

std::vector<Rect> view_parts(const Mark& m, double w, double h, int t) {
    std::vector<Rect> out;
    for (const auto& p : m.parts) out.push_back(turn_rect(p, w, h, t));
    return out;
}

int view_turn(const Mark& m, int t) { return norm(m.turn + t); }

Mark with_view_box(Mark m, const Rect& r, double w, double h, int t) {
    const Rect u = unturn_rect(r, w, h, t);
    m.x = u.x;
    m.y = u.y;
    m.w = u.w;
    m.h = u.h;
    return m;
}

Mark with_view_line(Mark m, Point a, Point b, double w, double h, int t) {
    const Point ua = unturn_point(a, w, h, t), ub = unturn_point(b, w, h, t);
    m.x = ua.x;
    m.y = ua.y;
    m.w = ub.x - ua.x;
    m.h = ub.y - ua.y;
    return m;
}

Mark moved(Mark m, double dx, double dy, int t) {
    const Point d = unturn_vector({dx, dy}, t);
    m.x += d.x;
    m.y += d.y;
    for (auto& p : m.parts) { p.x += d.x; p.y += d.y; }
    return m;
}

Mark signature(const std::string& art, double aw, double ah, double w, double h, int t) {
    const auto [sw, sh] = seen(w, h, t);
    aw = std::max(aw, 1.0);
    ah = std::max(ah, 1.0);
    double bw = sw / 3, bh = bw * ah / aw;
    if (bh > sh / 6) { bh = sh / 6; bw = bh * aw / ah; }
    Mark m;
    m.kind = MarkKind::Signature;
    m.art = art;
    m.turn = norm(-t);   // undo the page's turn: upright to the reader
    return with_view_box(m, {(sw - bw) / 2, sh * 2 / 3 - bh / 2, bw, bh}, w, h, t);
}

Mark boxed(MarkKind k, const Rect& r, std::uint32_t color, double width, double w, double h, int t) {
    Mark m;
    m.kind = k;
    m.color = color;
    m.width = width;
    m.turn = upright(k) ? norm(-t) : 0;
    return with_view_box(m, r, w, h, t);
}

Mark line(MarkKind k, Point a, Point b, std::uint32_t color, double width, double w, double h, int t) {
    Mark m;
    m.kind = k;
    m.color = color;
    m.width = width;
    return with_view_line(m, a, b, w, h, t);
}

Mark highlight(const std::vector<Rect>& seen_parts, const Rect& dragged, std::uint32_t color, double w, double h,
               int t) {
    Mark m;
    m.kind = MarkKind::Highlight;
    m.color = color;
    const auto parts = seen_parts.empty() ? std::vector<Rect>{dragged} : seen_parts;
    double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
    for (const auto& p : parts) {
        m.parts.push_back(unturn_rect(p, w, h, t));
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x + p.w);
        y1 = std::max(y1, p.y + p.h);
    }
    return with_view_box(m, {x0, y0, x1 - x0, y1 - y0}, w, h, t);
}

Mark note(Point at, const std::string& text, std::uint32_t color, double w, double h, int t) {
    const auto [sw, sh] = seen(w, h, t);
    const double x = std::clamp(at.x, 0.0, std::max(0.0, sw - kNoteSize));
    const double y = std::clamp(at.y, 0.0, std::max(0.0, sh - kNoteSize));
    Mark m = boxed(MarkKind::Note, {x, y, kNoteSize, kNoteSize}, color, 1, w, h, t);
    m.text = text;
    return m;
}

namespace {
double seg_dist(Point p, Point a, Point b) {
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    double k = l2 > 0 ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2 : 0;
    k = std::clamp(k, 0.0, 1.0);
    return std::hypot(p.x - (a.x + k * dx), p.y - (a.y + k * dy));
}
}  // namespace

Hit hit(const std::vector<Shape>& shapes, double x, double y, double tol) {
    for (int i = int(shapes.size()) - 1; i >= 0; --i) {
        const auto& s = shapes[std::size_t(i)];
        if (s.line) {
            if (std::hypot(x - s.b.x, y - s.b.y) <= tol) return {i, Part::End};
            if (std::hypot(x - s.a.x, y - s.a.y) <= tol) return {i, Part::Start};
            if (seg_dist({x, y}, s.a, s.b) <= tol) return {i, Part::Body};
            continue;
        }
        const auto& b = s.box;
        const double cx = b.x + b.w, cy = b.y + b.h;
        if (s.resizable && std::abs(x - cx) <= tol && std::abs(y - cy) <= tol) return {i, Part::Corner};
        if (x >= b.x && x <= cx && y >= b.y && y <= cy) return {i, Part::Body};
    }
    return {};
}

Point to_pdf(Point p, double llx, double lly, double urx, double ury, int rotate_deg) {
    const double cw = std::abs(urx - llx), ch = std::abs(ury - lly);
    const int r = norm(rotate_deg / 90);
    // Back to the crop box unturned (top-left), then flip y into PDF space.
    const Point u = unturn_point(p, cw, ch, r);
    return {std::min(llx, urx) + u.x, std::max(lly, ury) - u.y};
}

}  // namespace lookit::core::marks
