#include "core/Picture.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace lookit::core {

namespace picture {

namespace {
int norm(int t) { return ((t % 4) + 4) % 4; }
int clamp100(int v) { return std::clamp(v, -100, 100); }

// One quarter turn clockwise of a point in a frame W x H (the frame becomes H x W).
Pt turn1(Pt p, double /*W*/, double H) { return {H - p.y, p.x}; }

Rect span(Pt a, Pt b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::abs(b.x - a.x), std::abs(b.y - a.y)};
}

// `r` in a frame W x H, turned `q` quarters clockwise with it.
Rect turn_rect(Rect r, double W, double H, int q) {
    Pt a{r.x, r.y}, b{r.x + r.w, r.y + r.h};
    for (int i = 0; i < norm(q); ++i) {
        a = turn1(a, W, H);
        b = turn1(b, W, H);
        std::swap(W, H);
    }
    return span(a, b);
}
}  // namespace

std::pair<int, int> oriented(const PicEdit& e, int w, int h) {
    return norm(e.turn) % 2 ? std::pair{h, w} : std::pair{w, h};
}

Rect crop_of(const PicEdit& e, int w, int h) {
    if (e.crop.w > 0 && e.crop.h > 0) return e.crop;
    const auto [ow, oh] = oriented(e, w, h);
    return {0, 0, double(ow), double(oh)};
}

std::pair<int, int> out_size(const PicEdit& e, int w, int h) {
    if (e.out_w > 0 && e.out_h > 0) return {e.out_w, e.out_h};
    const Rect c = crop_of(e, w, h);
    return {std::max(1, int(std::lround(c.w))), std::max(1, int(std::lround(c.h)))};
}

Pt orient_point(Pt p, const PicEdit& e, int w, int h) {
    double W = w, H = h;
    if (e.flip) p.x = W - p.x;
    for (int i = 0; i < norm(e.turn); ++i) {
        p = turn1(p, W, H);
        std::swap(W, H);
    }
    return p;
}

bool lit(const PicEdit& e) { return e.brightness != 0 || e.contrast != 0 || e.saturation != 0; }
bool unchanged(const PicEdit& e) { return e == PicEdit{}; }

PicEdit turned(PicEdit e, int q, int w, int h) {
    q = norm(q);
    if (q == 0) return e;
    const auto [ow, oh] = oriented(e, w, h);
    if (e.crop.w > 0) e.crop = turn_rect(e.crop, ow, oh, q);
    if (q % 2) std::swap(e.out_w, e.out_h);
    e.turn = norm(e.turn + q);
    return e;
}

// With O = R^turn F^flip (F first): H.O = R^-turn F^(flip+1) and
// V.O = R^(2-turn) F^(flip+1), V being R^2 F. The crop mirrors with the frame.
PicEdit flipped(PicEdit e, bool horizontal, int w, int h) {
    const auto [ow, oh] = oriented(e, w, h);
    if (horizontal) {
        e.turn = norm(-e.turn);
        if (e.crop.w > 0) e.crop.x = ow - e.crop.x - e.crop.w;
    } else {
        e.turn = norm(2 - e.turn);
        if (e.crop.w > 0) e.crop.y = oh - e.crop.y - e.crop.h;
    }
    e.flip = !e.flip;
    return e;
}

PicEdit cropped(PicEdit e, const Rect& seen, int w, int h) {
    const Rect c = crop_of(e, w, h);
    const auto [sw, sh] = out_size(e, w, h);
    // What you see, kept on the picture.
    const double x0 = std::clamp(seen.x, 0.0, double(sw)), y0 = std::clamp(seen.y, 0.0, double(sh));
    const double x1 = std::clamp(seen.x + seen.w, 0.0, double(sw)), y1 = std::clamp(seen.y + seen.h, 0.0, double(sh));
    if (x1 - x0 < 1 || y1 - y0 < 1) return e;
    const double kx = c.w / sw, ky = c.h / sh;
    const auto [ow, oh] = oriented(e, w, h);
    // In the oriented frame, on whole pixels.
    double nx0 = std::round(c.x + x0 * kx), ny0 = std::round(c.y + y0 * ky);
    double nx1 = std::round(c.x + x1 * kx), ny1 = std::round(c.y + y1 * ky);
    nx0 = std::clamp(nx0, 0.0, double(ow) - 1);
    ny0 = std::clamp(ny0, 0.0, double(oh) - 1);
    nx1 = std::clamp(std::max(nx1, nx0 + 1), 1.0, double(ow));
    ny1 = std::clamp(std::max(ny1, ny0 + 1), 1.0, double(oh));
    const bool resized_before = e.out_w > 0;
    e.crop = {nx0, ny0, nx1 - nx0, ny1 - ny0};
    if (nx0 == 0 && ny0 == 0 && nx1 == ow && ny1 == oh) e.crop = {};
    if (resized_before) {
        // Resized: the new picture keeps the scale -- its size is what you framed.
        e.out_w = std::max(1, int(std::lround(x1 - x0)));
        e.out_h = std::max(1, int(std::lround(y1 - y0)));
        const Rect nc = crop_of(e, w, h);
        if (e.out_w == int(std::lround(nc.w)) && e.out_h == int(std::lround(nc.h))) e.out_w = e.out_h = 0;
    }
    return e;
}

PicEdit resized(PicEdit e, int ow, int oh, int w, int h) {
    if (ow < 1 || oh < 1) return e;
    const Rect c = crop_of(e, w, h);
    if (ow == int(std::lround(c.w)) && oh == int(std::lround(c.h))) e.out_w = e.out_h = 0;
    else { e.out_w = ow; e.out_h = oh; }
    return e;
}

PicEdit with_light(PicEdit e, int b, int c, int s) {
    e.brightness = clamp100(b);
    e.contrast = clamp100(c);
    e.saturation = clamp100(s);
    return e;
}

int keep_aspect(int side_new, int side, int other) {
    if (side <= 0) return other;
    return std::max(1, int(std::lround(double(side_new) * other / side)));
}

// Brightness shifts every channel (+-127 at the ends); contrast stretches
// about mid-grey (x0 .. x2); saturation pushes each channel away from (or
// toward) the pixel's luma (x0 grey .. x2). Clear pixels are left alone; a
// part-clear pixel is un-premultiplied, changed, premultiplied again.
void light_argb32(std::uint8_t* data, int w, int h, int stride, int b, int c, int s) {
    b = clamp100(b);
    c = clamp100(c);
    s = clamp100(s);
    if (!b && !c && !s) return;
    std::array<std::uint8_t, 256> lut{};
    const double cf = 1.0 + c / 100.0, shift = b * 1.27;
    for (int v = 0; v < 256; ++v)
        lut[std::size_t(v)] = std::uint8_t(std::clamp(std::lround((v - 128) * cf + 128 + shift), 0L, 255L));
    const double sf = 1.0 + s / 100.0;
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(data + std::size_t(y) * std::size_t(stride));
        for (int x = 0; x < w; ++x) {
            const std::uint32_t p = row[x];
            const int a = int(p >> 24);
            if (a == 0) continue;
            int ch[3] = {int((p >> 16) & 255), int((p >> 8) & 255), int(p & 255)};
            if (a < 255)
                for (int& v : ch) v = std::min(255, (v * 255 + a / 2) / a);
            for (int& v : ch) v = lut[std::size_t(v)];
            if (s) {
                const double l = 0.2126 * ch[0] + 0.7152 * ch[1] + 0.0722 * ch[2];
                for (int& v : ch) v = int(std::clamp(std::lround(l + (v - l) * sf), 0L, 255L));
            }
            if (a < 255)
                for (int& v : ch) v = (v * a + 127) / 255;
            row[x] = (std::uint32_t(a) << 24) | (std::uint32_t(ch[0]) << 16) | (std::uint32_t(ch[1]) << 8) |
                     std::uint32_t(ch[2]);
        }
    }
}

Grip crop_hit(const Rect& f, double x, double y, double tol) {
    auto near = [&](double cx, double cy) { return std::abs(x - cx) <= tol && std::abs(y - cy) <= tol; };
    if (near(f.x + f.w, f.y + f.h)) return Grip::BottomRight;
    if (near(f.x, f.y))             return Grip::TopLeft;
    if (near(f.x + f.w, f.y))       return Grip::TopRight;
    if (near(f.x, f.y + f.h))       return Grip::BottomLeft;
    if (x >= f.x && x <= f.x + f.w && y >= f.y && y <= f.y + f.h) return Grip::Body;
    return Grip::None;
}

Rect crop_drag(const Rect& s, Grip g, double dx, double dy, double pw, double ph, double min) {
    min = std::min({min, pw, ph});
    if (g == Grip::Body) {
        return {std::clamp(s.x + dx, 0.0, std::max(0.0, pw - s.w)), std::clamp(s.y + dy, 0.0, std::max(0.0, ph - s.h)),
                s.w, s.h};
    }
    if (g == Grip::None) return s;
    double x0 = s.x, y0 = s.y, x1 = s.x + s.w, y1 = s.y + s.h;
    const bool left = g == Grip::TopLeft || g == Grip::BottomLeft;
    const bool top = g == Grip::TopLeft || g == Grip::TopRight;
    if (left) x0 = std::clamp(x0 + dx, 0.0, x1 - min);
    else      x1 = std::clamp(x1 + dx, x0 + min, pw);
    if (top)  y0 = std::clamp(y0 + dy, 0.0, y1 - min);
    else      y1 = std::clamp(y1 + dy, y0 + min, ph);
    return {x0, y0, x1 - x0, y1 - y0};
}

bool natural_less(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            std::size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit(static_cast<unsigned char>(a[ie]))) ++ie;
            while (je < b.size() && std::isdigit(static_cast<unsigned char>(b[je]))) ++je;
            std::size_t is = i, js = j;
            while (is + 1 < ie && a[is] == '0') ++is;   // "007" is 7
            while (js + 1 < je && b[js] == '0') ++js;
            const std::string na = a.substr(is, ie - is), nb = b.substr(js, je - js);
            if (na.size() != nb.size()) return na.size() < nb.size();
            if (na != nb) return na < nb;
            i = ie;
            j = je;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    if ((a.size() - i) != (b.size() - j)) return (a.size() - i) < (b.size() - j);
    return a < b;
}

std::vector<std::string> sorted(std::vector<std::string> names) {
    std::sort(names.begin(), names.end(), natural_less);
    return names;
}

int index_of(const std::vector<std::string>& v, const std::string& current) {
    auto it = std::find(v.begin(), v.end(), current);
    return it == v.end() ? -1 : int(it - v.begin());
}

std::string step(const std::vector<std::string>& v, const std::string& current, int dir) {
    const int i = index_of(v, current);
    if (i < 0 || dir == 0) return {};
    const int n = i + (dir > 0 ? 1 : -1);
    return n >= 0 && n < int(v.size()) ? v[std::size_t(n)] : std::string{};
}

}  // namespace picture

// ── the store ────────────────────────────────────────────────────────────────

std::optional<PicEdit> PicStore::get(const Id& id) const {
    if (id != 0) return std::nullopt;
    return m_edit;
}

void PicStore::put(const Id& id, const std::optional<PicEdit>& v) {
    if (m_door && !m_door()) ++m_strays;   // a write outside the door
    if (id != 0) return;
    m_edit = v ? *v : PicEdit{};
}

void PicStore::load(int w, int h) {
    m_edit = {};
    m_w = std::max(1, w);
    m_h = std::max(1, h);
}

namespace pic {

namespace {
bool set(PicJournal& j, PicStore& s, const std::string& label, const PicEdit& next, const std::string& key = {}) {
    if (next == s.edit()) return false;
    return j.run(label, {0}, [&] { s.put(0, next); }, key);
}
}  // namespace

bool rotate(PicJournal& j, PicStore& s, int q) {
    if (((q % 4) + 4) % 4 == 0) return false;
    return set(j, s, q < 0 ? "Rotate Left" : "Rotate Right", picture::turned(s.edit(), q, s.w(), s.h()));
}

bool flip(PicJournal& j, PicStore& s, bool horizontal) {
    return set(j, s, horizontal ? "Flip Horizontally" : "Flip Vertically",
               picture::flipped(s.edit(), horizontal, s.w(), s.h()));
}

bool crop(PicJournal& j, PicStore& s, const layout::Rect& seen) {
    return set(j, s, "Crop", picture::cropped(s.edit(), seen, s.w(), s.h()));
}

bool resize(PicJournal& j, PicStore& s, int ow, int oh) {
    if (ow < 1 || oh < 1 || ow > 32768 || oh > 32768) return false;
    return set(j, s, "Resize", picture::resized(s.edit(), ow, oh, s.w(), s.h()));
}

bool light(PicJournal& j, PicStore& s, int b, int c, int sat, const std::string& key) {
    return set(j, s, "Adjust Light", picture::with_light(s.edit(), b, c, sat), key);
}

bool reset(PicJournal& j, PicStore& s) { return set(j, s, "Revert to Original", PicEdit{}); }

}  // namespace pic

}  // namespace lookit::core
