#include "Export.hpp"
#include "Log.hpp"
#include "core/Sign.hpp"

#include <cairo-pdf.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <fstream>
#include <memory>

namespace lookit::exporter {

const char* ext(Format f) {
    switch (f) {
        case Format::Png:  return "png";
        case Format::Jpeg: return "jpg";
        case Format::Tiff: return "tif";
        case Format::Bmp:  return "bmp";
        case Format::Pdf:  return "pdf";
        case Format::Text: return "txt";
    }
    return "";
}

const char* label(Format f) {
    switch (f) {
        case Format::Png:  return "PNG";
        case Format::Jpeg: return "JPEG";
        case Format::Tiff: return "TIFF";
        case Format::Bmp:  return "BMP";
        case Format::Pdf:  return "PDF";
        case Format::Text: return "Text (.txt)";
    }
    return "";
}

std::vector<Format> formats_for(const Document& doc) {
    if (doc.kind() == core::Kind::Image) return {Format::Png, Format::Jpeg, Format::Tiff, Format::Bmp, Format::Pdf};
    return {Format::Png, Format::Jpeg, Format::Tiff, Format::Text};
}

const std::vector<int>& dpis() {
    static const std::vector<int> d{72, 150, 300, 600};
    return d;
}

namespace {

struct SurfaceFree { void operator()(cairo_surface_t* s) const { cairo_surface_destroy(s); } };
using Surface = std::unique_ptr<cairo_surface_t, SurfaceFree>;

const char* pixbuf_type(Format f) {
    switch (f) {
        case Format::Png:  return "png";
        case Format::Jpeg: return "jpeg";
        case Format::Tiff: return "tiff";
        case Format::Bmp:  return "bmp";
        default:           return nullptr;
    }
}

bool save_surface(cairo_surface_t* s, Format f, bool opaque, const std::string& path, std::string& error) {
    const char* type = pixbuf_type(f);
    if (!type) { error = "not a picture format"; return false; }
    cairo_surface_flush(s);
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    // JPEG and BMP carry no alpha: onto white. Paper is opaque anyway.
    const bool flat = opaque || f == Format::Jpeg || f == Format::Bmp;
    std::vector<std::uint8_t> rgba(std::size_t(w) * std::size_t(h) * 4);
    core::convert::argb32_to_rgba(cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s), w, h,
                                  rgba.data(), w * 4, flat);
    GdkPixbuf* pb = nullptr;
    std::vector<std::uint8_t> rgb;
    if (flat) {   // three channels: every writer takes these
        rgb.resize(std::size_t(w) * std::size_t(h) * 3);
        for (std::size_t i = 0, n = std::size_t(w) * std::size_t(h); i < n; ++i)
            for (int c = 0; c < 3; ++c) rgb[i * 3 + std::size_t(c)] = rgba[i * 4 + std::size_t(c)];
        pb = gdk_pixbuf_new_from_data(rgb.data(), GDK_COLORSPACE_RGB, FALSE, 8, w, h, w * 3, nullptr, nullptr);
    } else {
        pb = gdk_pixbuf_new_from_data(rgba.data(), GDK_COLORSPACE_RGB, TRUE, 8, w, h, w * 4, nullptr, nullptr);
    }
    GError* err = nullptr;
    gboolean ok = FALSE;
    if (f == Format::Jpeg) ok = gdk_pixbuf_save(pb, path.c_str(), type, &err, "quality", "92", nullptr);
    else if (f == Format::Tiff) ok = gdk_pixbuf_save(pb, path.c_str(), type, &err, "compression", "8", nullptr);
    else ok = gdk_pixbuf_save(pb, path.c_str(), type, &err, nullptr);
    g_object_unref(pb);
    if (!ok) {
        error = err ? err->message : "the picture writer failed";
        if (err) g_error_free(err);
        return false;
    }
    return true;
}

}  // namespace

bool write_page(const Document& doc, int page, int dpi, Format f, const std::string& path, std::string& error) {
    if (page < 0 || page >= doc.count()) { error = "no such page"; return false; }
    const bool picture = doc.kind() == core::Kind::Image;
    const auto sz = doc.size(page);
    const double scale = picture ? 1.0 : dpi / 72.0;
    const auto [w, h] = picture ? std::pair{int(sz.w), int(sz.h)} : core::convert::pixels(sz.w, sz.h, dpi);
    Surface s(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h));
    if (cairo_surface_status(s.get()) != CAIRO_STATUS_SUCCESS) {
        error = "the page is too large at " + std::to_string(dpi) + " dpi";
        return false;
    }
    cairo_t* cr = cairo_create(s.get());
    doc.render(cr, page, scale, true);
    cairo_destroy(cr);
    return save_surface(s.get(), f, doc.opaque(), path, error);
}

bool write_picture_pdf(const Document& doc, const std::string& path, std::string& error) {
    if (doc.count() < 1) { error = "nothing to write"; return false; }
    const auto sz = doc.size(0);
    const double k = 72.0 / 150.0;
    Surface pdf(cairo_pdf_surface_create(path.c_str(), sz.w * k, sz.h * k));
    cairo_t* cr = cairo_create(pdf.get());
    doc.render(cr, 0, k, true);
    cairo_destroy(cr);
    cairo_surface_finish(pdf.get());
    if (cairo_surface_status(pdf.get()) != CAIRO_STATUS_SUCCESS) {
        error = cairo_status_to_string(cairo_surface_status(pdf.get()));
        return false;
    }
    return true;
}

bool write_text(const Document& doc, const std::vector<int>& pages, const std::string& path, std::string& error) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { error = "can't write " + path; return false; }
    for (int p : pages) {
        if (p < 0 || p >= doc.count()) continue;
        std::string t = doc.text(p);
        out << t;
        if (!t.empty() && t.back() != '\n') out << '\n';
        out << '\f';
    }
    out.close();
    if (!out) { error = "writing " + path + " failed"; return false; }
    return true;
}

}  // namespace lookit::exporter
