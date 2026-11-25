#include "Document.hpp"
#include "Log.hpp"

#include <gdkmm/texture.h>
#include <giomm/file.h>
#include <glibmm/convert.h>
#include <glibmm/error.h>
#include <poppler.h>

#include <algorithm>
#include <cmath>
#include <map>
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
// A composite: each page is (source document, page number, quarter turns), so
// the page tools' edited list (core::Pages) and a freshly opened file are the
// same thing. Poppler documents are shared through a small pool, so rebuilding
// after an edit re-reads nothing.
struct Poppler {
    PopplerDocument* doc = nullptr;
    std::vector<PopplerPage*> pages;
    ~Poppler() {
        for (auto* p : pages) if (p) g_object_unref(p);
        if (doc) g_object_unref(doc);
    }
};

std::map<std::string, std::weak_ptr<Poppler>>& poppler_pool() {
    static std::map<std::string, std::weak_ptr<Poppler>> pool;
    return pool;
}

std::shared_ptr<Poppler> load_poppler(const std::string& path, std::string& error) {
    auto& pool = poppler_pool();
    if (auto it = pool.find(path); it != pool.end())
        if (auto live = it->second.lock()) return live;
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
    auto p = std::make_shared<Poppler>();
    p->doc = doc;
    const int n = poppler_document_get_n_pages(doc);
    for (int i = 0; i < n; ++i) p->pages.push_back(poppler_document_get_page(doc, i));
    pool[path] = p;
    return p;
}

class PdfDocument final : public Document {
public:
    struct Page { std::shared_ptr<Poppler> src; int index; int turn; };

    PdfDocument(std::vector<Page> pages, std::string path, std::string title) : m_pages(std::move(pages)) {
        m_kind = core::Kind::Pdf;
        m_path = std::move(path);
        m_title = std::move(title);
        for (const auto& pg : m_pages) {
            double w = 612, h = 792;   // a page poppler can't give: US Letter, not a crash
            if (auto* p = raw(pg)) poppler_page_get_size(p, &w, &h);
            m_base.push_back({w, h});
        }
    }
    int count() const override { return int(m_pages.size()); }
    core::layout::Size size(int i) const override {
        const auto b = m_base.at(std::size_t(i));
        return m_pages[std::size_t(i)].turn % 2 ? core::layout::Size{b.h, b.w} : b;
    }
    std::vector<core::search::Hit> find(int i, const std::string& text) const override {
        std::vector<core::search::Hit> out;
        PopplerPage* p = raw(m_pages.at(std::size_t(i)));
        if (!p || text.empty()) return out;
        const auto b = m_base[std::size_t(i)];
        const int turn = m_pages[std::size_t(i)].turn;
        GList* rects = poppler_page_find_text_with_options(p, text.c_str(), POPPLER_FIND_DEFAULT);
        for (GList* l = rects; l; l = l->next) {
            auto* r = static_cast<PopplerRectangle*>(l->data);
            const auto flat = core::search::from_pdf(r->x1, r->y1, r->x2, r->y2, b.h);
            out.push_back({i, core::search::turn_rect(flat, b.w, b.h, turn)});
            poppler_rectangle_free(r);
        }
        g_list_free(rects);
        core::search::sort(out);
        return out;
    }
    void render(cairo_t* cr, int i, double scale) const override {
        const auto& pg = m_pages.at(std::size_t(i));
        const auto b = m_base[std::size_t(i)];
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        // Quarter turns clockwise, about the page's own corner.
        switch (pg.turn) {
            case 1: cairo_translate(cr, b.h, 0);   cairo_rotate(cr, G_PI / 2); break;
            case 2: cairo_translate(cr, b.w, b.h); cairo_rotate(cr, G_PI); break;
            case 3: cairo_translate(cr, 0, b.w);   cairo_rotate(cr, 3 * G_PI / 2); break;
            default: break;
        }
        cairo_rectangle(cr, 0, 0, b.w, b.h);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
        if (auto* p = raw(pg)) poppler_page_render(p, cr);
        cairo_restore(cr);
    }

private:
    static PopplerPage* raw(const Page& pg) {
        return pg.src && pg.index >= 0 && pg.index < int(pg.src->pages.size())
                   ? pg.src->pages[std::size_t(pg.index)] : nullptr;
    }
    std::vector<Page> m_pages;
    std::vector<core::layout::Size> m_base;   // unturned sizes
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
    auto pop = load_poppler(path, error);
    if (!pop) return nullptr;
    std::string title;
    if (gchar* t = poppler_document_get_title(pop->doc)) { title = t; g_free(t); }
    std::vector<PdfDocument::Page> pages;
    for (int i = 0; i < int(pop->pages.size()); ++i) pages.push_back({pop, i, 0});
    return std::make_unique<PdfDocument>(std::move(pages), path, title);
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

Glib::RefPtr<Gdk::Texture> render_texture(const Document& doc, int i, double s, double max_pixels) {
    const auto sz = doc.size(i);
    double rs = s;
    if (sz.w * sz.h * rs * rs > max_pixels) rs = std::sqrt(max_pixels / (sz.w * sz.h));
    const int w = std::max(1, int(std::ceil(sz.w * rs)));
    const int h = std::max(1, int(std::ceil(sz.h * rs)));
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    doc.render(cr, i, rs);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    GBytes* bytes = g_bytes_new(cairo_image_surface_get_data(surf), std::size_t(stride) * std::size_t(h));
    cairo_surface_destroy(surf);
    // GDK_MEMORY_DEFAULT is premultiplied BGRA (little-endian) -- Cairo's ARGB32.
    GdkTexture* t = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, std::size_t(stride));
    g_bytes_unref(bytes);
    return Glib::wrap(t);
}

void Document::forget(const std::string& path) { poppler_pool().erase(path); }

std::shared_ptr<Document> Document::compose(const std::vector<std::string>& sources,
                                            const std::vector<core::PageRec>& recs, const std::string& path,
                                            std::string& error) {
    std::vector<std::shared_ptr<Poppler>> pops;
    for (const auto& s : sources) {
        std::string e;
        pops.push_back(load_poppler(s, e));   // a source that fails gives blank pages, not a crash
        if (!pops.back() && error.empty()) error = e;
    }
    std::vector<PdfDocument::Page> pages;
    for (const auto& r : recs)
        pages.push_back({r.src >= 0 && r.src < int(pops.size()) ? pops[std::size_t(r.src)] : nullptr, r.index, r.turn});
    return std::make_shared<PdfDocument>(std::move(pages), path, "");
}

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
