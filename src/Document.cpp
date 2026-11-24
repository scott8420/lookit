#include "Document.hpp"
#include "Log.hpp"

#include <gdkmm/texture.h>
#include <giomm/file.h>
#include <glibmm/convert.h>
#include <glibmm/error.h>
#include <poppler.h>

#include <vector>

// Document.cpp -- the kinds Lookit can draw. Each backend opens, measures, and
// renders onto Cairo; the view caches what they draw (DocView).

namespace lookit {

std::string Document::name() const {
    const auto slash = m_path.find_last_of('/');
    return slash == std::string::npos ? m_path : m_path.substr(slash + 1);
}

namespace {

// ── PDF: poppler-glib ───────────────────────────────────────────────────────
class PdfDocument final : public Document {
public:
    PdfDocument(PopplerDocument* doc, std::string path) : m_doc(doc) {
        m_kind = core::Kind::Pdf;
        m_path = std::move(path);
        if (gchar* t = poppler_document_get_title(m_doc)) { m_title = t; g_free(t); }
        const int n = poppler_document_get_n_pages(m_doc);
        m_pages.reserve(std::size_t(n));
        m_sizes.reserve(std::size_t(n));
        for (int i = 0; i < n; ++i) {
            PopplerPage* p = poppler_document_get_page(m_doc, i);
            double w = 612, h = 792;   // a page poppler can't give: US Letter, not a crash
            if (p) poppler_page_get_size(p, &w, &h);
            m_pages.push_back(p);
            m_sizes.push_back({w, h});
        }
    }
    ~PdfDocument() override {
        for (auto* p : m_pages) if (p) g_object_unref(p);
        g_object_unref(m_doc);
    }
    int count() const override { return int(m_pages.size()); }
    core::layout::Size size(int i) const override { return m_sizes.at(std::size_t(i)); }
    void render(cairo_t* cr, int i, double scale) const override {
        PopplerPage* p = m_pages.at(std::size_t(i));
        const auto s = m_sizes.at(std::size_t(i));
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_rectangle(cr, 0, 0, s.w, s.h);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
        if (p) poppler_page_render(p, cr);
        cairo_restore(cr);
    }

private:
    PopplerDocument* m_doc;
    std::vector<PopplerPage*> m_pages;
    std::vector<core::layout::Size> m_sizes;
};

// ── Pictures: GdkTexture (glycin / gdk-pixbuf underneath) ───────────────────
// Decoded once into a Cairo surface; drawing is then a scaled paint.
class ImageDocument final : public Document {
public:
    ImageDocument(cairo_surface_t* surf, int w, int h, std::string path)
        : m_surf(surf), m_w(w), m_h(h) {
        m_kind = core::Kind::Image;
        m_path = std::move(path);
    }
    ~ImageDocument() override { cairo_surface_destroy(m_surf); }
    int count() const override { return 1; }
    core::layout::Size size(int) const override { return {double(m_w), double(m_h)}; }
    bool opaque() const override { return false; }
    void render(cairo_t* cr, int, double scale) const override {
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, m_surf, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr),
                                 scale < 1.0 ? CAIRO_FILTER_GOOD
                                 : scale >= 3.0 ? CAIRO_FILTER_NEAREST   // see the pixels
                                                : CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);
    }

private:
    cairo_surface_t* m_surf;
    int m_w, m_h;
};

std::unique_ptr<Document> open_pdf(const std::string& path, std::string& error) {
    GError* err = nullptr;
    std::string uri;
    try { uri = Glib::filename_to_uri(path); }
    catch (const Glib::Error& e) { error = e.what(); return nullptr; }
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), nullptr, &err);
    if (!doc) {
        if (err && err->domain == POPPLER_ERROR && err->code == POPPLER_ERROR_ENCRYPTED)
            error = "This PDF is locked with a password. (Opening locked PDFs comes later.)";
        else
            error = std::string("Poppler could not read it: ") + (err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return nullptr;
    }
    return std::make_unique<PdfDocument>(doc, path);
}

std::unique_ptr<Document> open_image(const std::string& path, std::string& error) {
    Glib::RefPtr<Gdk::Texture> tex;
    try {
        tex = Gdk::Texture::create_from_file(Gio::File::create_for_path(path));
    } catch (const Glib::Error& e) {
        error = std::string("Could not read the picture: ") + e.what();
        return nullptr;
    }
    const int w = tex->get_width(), h = tex->get_height();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        error = "The picture is too large to hold in memory.";
        return nullptr;
    }
    cairo_surface_flush(surf);
    // GdkTexture's default download format is premultiplied BGRA/ARGB in
    // native order -- exactly CAIRO_FORMAT_ARGB32.
    tex->download(cairo_image_surface_get_data(surf),
                  std::size_t(cairo_image_surface_get_stride(surf)));
    cairo_surface_mark_dirty(surf);
    return std::make_unique<ImageDocument>(surf, w, h, path);
}

}  // namespace

std::unique_ptr<Document> Document::open(const std::string& path, std::string& error) {
    const core::Kind k = core::detect(path);
    if (auto lg = log::get(log::Area::Io)) lg->info("open {} as {}", path, core::kind_label(k));
    switch (k) {
        case core::Kind::Pdf:   return open_pdf(path, error);
        case core::Kind::Image: return open_image(path, error);
        case core::Kind::DjVu:
        case core::Kind::Epub:
        case core::Kind::Cbz:
            error = std::string(core::kind_label(k)) + " files are on the road but not here yet.";
            return nullptr;
        default:
            error = "Lookit doesn't know what kind of file this is.";
            return nullptr;
    }
}

}  // namespace lookit
