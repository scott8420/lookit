#include "MarkArt.hpp"

#include <glib.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace lookit::mark_art {

using core::Mark;
using core::MarkKind;

namespace {
struct Free { void operator()(cairo_surface_t* s) const { if (s) cairo_surface_destroy(s); } };
std::map<std::string, std::unique_ptr<cairo_surface_t, Free>>& cache() {
    static std::map<std::string, std::unique_ptr<cairo_surface_t, Free>> c;
    return c;
}

void rgba(cairo_t* cr, std::uint32_t c, double alpha = 1.0) {
    cairo_set_source_rgba(cr, ((c >> 24) & 0xff) / 255.0, ((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0,
                          (c & 0xff) / 255.0 * alpha);
}

// Into the mark's own upright frame: returns its sides there.
std::pair<double, double> own_frame(cairo_t* cr, const Mark& m) {
    const int t = ((m.turn % 4) + 4) % 4;
    cairo_translate(cr, m.x, m.y);
    turn_frame(cr, t, t % 2 ? m.h : m.w, t % 2 ? m.w : m.h);
    return t % 2 ? std::pair{m.h, m.w} : std::pair{m.w, m.h};
}

PangoLayout* layout_for(cairo_t* cr, const Mark& m, double width) {
    PangoLayout* l = pango_cairo_create_layout(cr);
    cairo_font_options_t* fo = cairo_font_options_create();
    cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
    pango_cairo_context_set_font_options(pango_layout_get_context(l), fo);
    cairo_font_options_destroy(fo);
    pango_layout_context_changed(l);
    PangoFontDescription* fd = pango_font_description_from_string("Sans");
    pango_font_description_set_absolute_size(fd, m.size * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);
    pango_layout_set_width(l, int(std::max(1.0, width) * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_text(l, m.text.c_str(), -1);
    return l;
}

constexpr double kTextPad = 3;   // points round a text box's text

void draw_note(cairo_t* cr, double s, std::uint32_t c) {
    // A sticky note: a square with its corner folded, three lines of "text".
    const double f = s * 0.3;
    cairo_move_to(cr, 0, 0);
    cairo_line_to(cr, s, 0);
    cairo_line_to(cr, s, s - f);
    cairo_line_to(cr, s - f, s);
    cairo_line_to(cr, 0, s);
    cairo_close_path(cr);
    rgba(cr, c);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_set_line_width(cr, 0.8);
    cairo_stroke(cr);
    cairo_move_to(cr, s, s - f);
    cairo_line_to(cr, s - f, s - f);
    cairo_line_to(cr, s - f, s);
    cairo_stroke(cr);
    for (int i = 0; i < 3; ++i) {
        const double y = s * (0.28 + 0.2 * i);
        cairo_move_to(cr, s * 0.2, y);
        cairo_line_to(cr, s * (i == 2 ? 0.55 : 0.8), y);
    }
    cairo_set_line_width(cr, s * 0.07);
    cairo_stroke(cr);
}
}  // namespace

cairo_surface_t* surface(const std::string& path) {
    auto& c = cache();
    if (auto it = c.find(path); it != c.end()) return it->second.get();
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(s);
        s = nullptr;
    }
    c[path].reset(s);
    return s;
}

std::pair<int, int> size(const std::string& path) {
    auto* s = surface(path);
    if (!s) return {0, 0};
    return {cairo_image_surface_get_width(s), cairo_image_surface_get_height(s)};
}

void turn_frame(cairo_t* cr, int turn, double bw, double bh) {
    switch (((turn % 4) + 4) % 4) {   // quarter turns clockwise, about the corner
        case 1: cairo_translate(cr, bh, 0);  cairo_rotate(cr, G_PI / 2); break;
        case 2: cairo_translate(cr, bw, bh); cairo_rotate(cr, G_PI); break;
        case 3: cairo_translate(cr, 0, bw);  cairo_rotate(cr, 3 * G_PI / 2); break;
        default: break;
    }
}

void draw(cairo_t* cr, const Mark& m, Notes notes, Blend blend) {
    cairo_save(cr);
    switch (m.kind) {
        case MarkKind::Signature: {
            auto* art = surface(m.art);
            if (!art || m.w <= 0 || m.h <= 0) break;
            const int aw = cairo_image_surface_get_width(art), ah = cairo_image_surface_get_height(art);
            const auto [uw, uh] = own_frame(cr, m);
            cairo_scale(cr, uw / aw, uh / ah);
            cairo_set_source_surface(cr, art, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            break;
        }
        case MarkKind::Highlight: {
            if (blend == Blend::Multiply) {
                cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
                rgba(cr, m.color);
            } else {
                rgba(cr, m.color, 0.4);
            }
            for (const auto& p : m.parts) cairo_rectangle(cr, p.x, p.y, p.w, p.h);
            cairo_fill(cr);
            break;
        }
        case MarkKind::Note: {
            if (notes == Notes::Skip) break;
            const auto [uw, uh] = own_frame(cr, m);
            (void)uh;
            draw_note(cr, uw, m.color);
            break;
        }
        case MarkKind::Box:
        case MarkKind::Ellipse: {
            const double inset = m.width / 2;
            const double w = std::max(0.0, m.w - m.width), h = std::max(0.0, m.h - m.width);
            if (m.kind == MarkKind::Box) {
                cairo_rectangle(cr, m.x + inset, m.y + inset, w, h);
            } else if (w > 0 && h > 0) {
                cairo_save(cr);
                cairo_translate(cr, m.x + m.w / 2, m.y + m.h / 2);
                cairo_scale(cr, w / 2, h / 2);
                cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
                cairo_restore(cr);
            }
            rgba(cr, m.color);
            cairo_set_line_width(cr, m.width);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_stroke(cr);
            break;
        }
        case MarkKind::Line:
        case MarkKind::Arrow: {
            const double x1 = m.x + m.w, y1 = m.y + m.h;
            const double len = std::hypot(m.w, m.h);
            const double head = std::max(8.0, m.width * 4);
            double ex = x1, ey = y1;
            if (m.kind == MarkKind::Arrow && len > 0) {   // stop the shaft inside the head
                ex -= m.w / len * head * 0.6;
                ey -= m.h / len * head * 0.6;
            }
            rgba(cr, m.color);
            cairo_set_line_width(cr, m.width);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, m.x, m.y);
            cairo_line_to(cr, ex, ey);
            cairo_stroke(cr);
            if (m.kind == MarkKind::Arrow && len > 0) {
                const double ux = m.w / len, uy = m.h / len;
                const double bx = x1 - ux * head, by = y1 - uy * head;
                cairo_move_to(cr, x1, y1);
                cairo_line_to(cr, bx - uy * head * 0.5, by + ux * head * 0.5);
                cairo_line_to(cr, bx + uy * head * 0.5, by - ux * head * 0.5);
                cairo_close_path(cr);
                cairo_fill(cr);
            }
            break;
        }
        case MarkKind::Text: {
            const auto [uw, uh] = own_frame(cr, m);
            (void)uh;
            PangoLayout* l = layout_for(cr, m, uw - 2 * kTextPad);
            cairo_move_to(cr, kTextPad, kTextPad);
            rgba(cr, m.color);
            pango_cairo_show_layout(cr, l);
            g_object_unref(l);
            break;
        }
    }
    cairo_restore(cr);
}

double text_height(const Mark& m) {
    const int t = ((m.turn % 4) + 4) % 4;
    const double uw = t % 2 ? m.h : m.w;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(s);
    PangoLayout* l = layout_for(cr, m, uw - 2 * kTextPad);
    PangoRectangle ink, logical;
    pango_layout_get_extents(l, &ink, &logical);
    g_object_unref(l);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    return std::max(m.size, double(logical.height) / PANGO_SCALE) + 2 * kTextPad;
}

}  // namespace lookit::mark_art
