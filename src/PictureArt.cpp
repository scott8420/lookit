#include "PictureArt.hpp"

#include <cmath>
#include <cstring>

namespace lookit::picture_art {

namespace PI = core::picture;

namespace {
cairo_surface_t* copy(cairo_surface_t* s) {
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(s);
    cairo_surface_flush(out);
    const int si = cairo_image_surface_get_stride(s), so = cairo_image_surface_get_stride(out);
    const unsigned char* a = cairo_image_surface_get_data(s);
    unsigned char* b = cairo_image_surface_get_data(out);
    if (a && b)
        for (int y = 0; y < h; ++y) std::memcpy(b + std::size_t(y) * std::size_t(so), a + std::size_t(y) * std::size_t(si), std::size_t(w) * 4);
    cairo_surface_mark_dirty(out);
    return out;
}
}  // namespace

cairo_surface_t* shape(cairo_surface_t* src, const core::PicEdit& e) {
    const int w = cairo_image_surface_get_width(src), h = cairo_image_surface_get_height(src);
    core::PicEdit geo = e;
    geo.brightness = geo.contrast = geo.saturation = 0;
    if (PI::unchanged(geo)) return copy(src);
    const auto c = PI::crop_of(e, w, h);
    const auto [ow, oh] = PI::out_size(e, w, h);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
    if (cairo_surface_status(out) != CAIRO_STATUS_SUCCESS) return out;
    cairo_t* cr = cairo_create(out);
    const double sx = ow / c.w, sy = oh / c.h;
    cairo_scale(cr, sx, sy);
    cairo_translate(cr, -c.x, -c.y);
    // The orientation as an affine map, from where three points land.
    const auto o = PI::orient_point({0, 0}, e, w, h);
    const auto ox = PI::orient_point({1, 0}, e, w, h);
    const auto oy = PI::orient_point({0, 1}, e, w, h);
    cairo_matrix_t m;
    cairo_matrix_init(&m, ox.x - o.x, ox.y - o.y, oy.x - o.x, oy.y - o.y, o.x, o.y);
    cairo_transform(cr, &m);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_t* p = cairo_get_source(cr);
    cairo_pattern_set_extend(p, CAIRO_EXTEND_PAD);   // no dark seam at the edges
    const bool exact = std::abs(sx - 1) < 1e-9 && std::abs(sy - 1) < 1e-9;
    cairo_pattern_set_filter(p, exact ? CAIRO_FILTER_NEAREST : sx < 1 || sy < 1 ? CAIRO_FILTER_GOOD
                                                                               : CAIRO_FILTER_BILINEAR);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(out);
    return out;
}

cairo_surface_t* light(cairo_surface_t* shaped, const core::PicEdit& e) {
    cairo_surface_t* out = copy(shaped);
    if (!PI::lit(e)) return out;
    cairo_surface_flush(out);
    PI::light_argb32(cairo_image_surface_get_data(out), cairo_image_surface_get_width(out),
                     cairo_image_surface_get_height(out), cairo_image_surface_get_stride(out), e.brightness,
                     e.contrast, e.saturation);
    cairo_surface_mark_dirty(out);
    return out;
}

cairo_surface_t* apply(cairo_surface_t* src, const core::PicEdit& e) {
    cairo_surface_t* s = shape(src, e);
    if (!PI::lit(e)) return s;
    cairo_surface_t* l = light(s, e);
    cairo_surface_destroy(s);
    return l;
}

}  // namespace lookit::picture_art
