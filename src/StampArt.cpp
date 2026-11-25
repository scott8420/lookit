#include "StampArt.hpp"

#include <glib.h>

#include <map>
#include <memory>

namespace lookit::stamp_art {

namespace {
struct Free { void operator()(cairo_surface_t* s) const { if (s) cairo_surface_destroy(s); } };
std::map<std::string, std::unique_ptr<cairo_surface_t, Free>>& cache() {
    static std::map<std::string, std::unique_ptr<cairo_surface_t, Free>> c;
    return c;
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

void draw(cairo_t* cr, const core::Stamp& s) {
    auto* art = surface(s.art);
    if (!art || s.w <= 0 || s.h <= 0) return;
    const int aw = cairo_image_surface_get_width(art), ah = cairo_image_surface_get_height(art);
    if (aw <= 0 || ah <= 0) return;
    const int t = ((s.turn % 4) + 4) % 4;
    // The picture's own upright frame: a quarter turn swaps the box's sides.
    const double uw = t % 2 ? s.h : s.w, uh = t % 2 ? s.w : s.h;
    cairo_save(cr);
    cairo_translate(cr, s.x, s.y);
    switch (t) {   // the same turns as a page (Document.cpp), about the box's corner
        case 1: cairo_translate(cr, s.w, 0);   cairo_rotate(cr, G_PI / 2); break;
        case 2: cairo_translate(cr, s.w, s.h); cairo_rotate(cr, G_PI); break;
        case 3: cairo_translate(cr, 0, s.h);   cairo_rotate(cr, 3 * G_PI / 2); break;
        default: break;
    }
    cairo_scale(cr, uw / aw, uh / ah);
    cairo_set_source_surface(cr, art, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

}  // namespace lookit::stamp_art
