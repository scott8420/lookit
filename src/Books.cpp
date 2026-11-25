#include "Books.hpp"
#include "Log.hpp"
#include "core/Book.hpp"
#include "core/Zip.hpp"

#include <gdkmm/texture.h>
#include <gio/gio.h>
#include <glibmm/bytes.h>
#include <glibmm/error.h>
#include <libdjvu/ddjvuapi.h>
#include <libdjvu/miniexp.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <map>

// Books.cpp -- the kinds that are not PDFs or single pictures (s012):
//   DjVu  -- djvulibre's ddjvuapi: pages, sizes from each page's dpi, the
//            text layer (words with boxes) for find and text export.
//   CBZ   -- a ZIP of pictures (core/Zip + core/Book): sizes from the
//            pictures' headers, each picture decoded when its page is drawn
//            (a few kept).
//   EPUB  -- a ZIP of XHTML: the cover, then the chapters' text laid out by
//            Pango into 6 x 9 in pages. A look inside, not a reading app.
// All three are Documents like any other: the view, thumbnails, Book view,
// export and quick look need nothing new. They don't edit (no page tools).

namespace lookit::books {

namespace {

// ── shared: a whole file in memory, raw deflate, a picture's pixels ─────────
bool slurp(const std::string& path, std::string& out, std::string& error) {
    gchar* data = nullptr;
    gsize len = 0;
    GError* err = nullptr;
    if (!g_file_get_contents(path.c_str(), &data, &len, &err)) {
        error = std::string("Couldn't read it: ") + (err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return false;
    }
    out.assign(data, len);
    g_free(data);
    return true;
}

// An entry's bytes, inflated if it is deflated (GIO's zlib -- no new dep).
std::optional<std::string> unpack(const std::string& zip, const core::zip::Entry& e) {
    auto raw = core::zip::raw(zip, e);
    if (!raw || e.method == 0) return raw;
    if (e.method != 8) return std::nullopt;
    GZlibDecompressor* z = g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);
    std::string out;
    out.resize(std::max<std::size_t>(e.size, 1));
    gsize in_at = 0, out_at = 0;
    bool ok = false;
    for (;;) {
        gsize read = 0, written = 0;
        GError* err = nullptr;
        if (out_at == out.size()) out.resize(out.size() * 2);
        const auto r = g_converter_convert(G_CONVERTER(z), raw->data() + in_at, raw->size() - in_at,
                                           out.data() + out_at, out.size() - out_at, G_CONVERTER_INPUT_AT_END,
                                           &read, &written, &err);
        in_at += read;
        out_at += written;
        if (r == G_CONVERTER_ERROR) {
            const bool no_space = err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_NO_SPACE);
            if (err) g_error_free(err);
            if (no_space) { out.resize(out.size() * 2); continue; }
            break;
        }
        if (r == G_CONVERTER_FINISHED) { ok = true; break; }
    }
    g_object_unref(z);
    if (!ok) return std::nullopt;
    out.resize(out_at);
    return out;
}

// Encoded picture bytes -> an ARGB32 surface (null when GDK can't read them).
cairo_surface_t* decode(const std::string& bytes) {
    Glib::RefPtr<Gdk::Texture> tex;
    try {
        tex = Gdk::Texture::create_from_bytes(Glib::Bytes::create(bytes.data(), bytes.size()));
    } catch (const Glib::Error&) {
        return nullptr;
    }
    if (!tex) return nullptr;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tex->get_width(), tex->get_height());
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); return nullptr; }
    cairo_surface_flush(s);
    tex->download(cairo_image_surface_get_data(s), std::size_t(cairo_image_surface_get_stride(s)));
    cairo_surface_mark_dirty(s);
    return s;
}

// A few decoded pictures, the newest kept (comic pages, a book's cover).
class Pictures {
public:
    ~Pictures() { for (auto& [k, s] : m_lru) cairo_surface_destroy(s); }
    cairo_surface_t* get(int key) const {
        for (auto it = m_lru.begin(); it != m_lru.end(); ++it)
            if (it->first == key) { m_lru.splice(m_lru.begin(), m_lru, it); return m_lru.front().second; }
        return nullptr;
    }
    void put(int key, cairo_surface_t* s) const {
        m_lru.push_front({key, s});
        while (m_lru.size() > 6) { cairo_surface_destroy(m_lru.back().second); m_lru.pop_back(); }
    }
private:
    mutable std::list<std::pair<int, cairo_surface_t*>> m_lru;
};

// A picture fitted (or drawn 1:1) in a page of `w` x `h` units at `scale`.
void paint_picture(cairo_t* cr, cairo_surface_t* s, double w, double h, double scale, bool fit) {
    cairo_save(cr);
    cairo_scale(cr, scale, scale);
    if (!s) {   // a page that wouldn't decode: grey, not a crash
        cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }
    const double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
    double k = w / iw;
    double ox = 0, oy = 0;
    if (fit) {
        k = std::min(w / iw, h / ih);
        ox = (w - iw * k) / 2;
        oy = (h - ih * k) / 2;
    }
    cairo_translate(cr, ox, oy);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), scale * k < 1.0 ? CAIRO_FILTER_GOOD : CAIRO_FILTER_BILINEAR);
    cairo_paint(cr);
    cairo_restore(cr);
}

// ── DjVu ────────────────────────────────────────────────────────────────────
void djvu_pump(ddjvu_context_t* ctx, bool wait, std::string* error = nullptr) {
    if (wait) ddjvu_message_wait(ctx);
    while (const ddjvu_message_t* m = ddjvu_message_peek(ctx)) {
        if (m->m_any.tag == DDJVU_ERROR && error && error->empty() && m->m_error.message)
            *error = m->m_error.message;
        ddjvu_message_pop(ctx);
    }
}

class DjvuDocument final : public Document {
public:
    struct PageInfo { double w = 612, h = 792; int dpi = 300; int pw = 0, ph = 0; };

    DjvuDocument(ddjvu_context_t* ctx, ddjvu_document_t* doc, std::vector<PageInfo> pages, std::string path)
        : m_ctx(ctx), m_doc(doc), m_pages(std::move(pages)) {
        m_kind = core::Kind::DjVu;
        m_path = std::move(path);
        m_words.resize(m_pages.size());
    }
    ~DjvuDocument() override {
        ddjvu_document_release(m_doc);
        ddjvu_context_release(m_ctx);
    }
    int count() const override { return int(m_pages.size()); }
    core::layout::Size size(int i) const override {
        const auto& p = m_pages.at(std::size_t(i));
        return {p.w, p.h};
    }
    bool searchable() const override { return true; }
    void render(cairo_t* cr, int i, double scale, bool) const override {
        const auto& info = m_pages.at(std::size_t(i));
        const int W = std::max(1, int(std::ceil(info.w * scale))), H = std::max(1, int(std::ceil(info.h * scale)));
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        bool ok = false;
        if (ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc, i)) {
            while (!ddjvu_page_decoding_done(page)) djvu_pump(m_ctx, true);
            if (!ddjvu_page_decoding_error(page)) {
                unsigned int masks[4] = {0xff0000, 0xff00, 0xff, 0xff000000};
                ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 4, masks);
                ddjvu_format_set_row_order(fmt, 1);   // top row first, like Cairo
                ddjvu_format_set_y_direction(fmt, 1);
                ddjvu_rect_t rect{0, 0, unsigned(W), unsigned(H)};
                cairo_surface_flush(s);
                auto* px = cairo_image_surface_get_data(s);
                const int stride = cairo_image_surface_get_stride(s);
                ok = ddjvu_page_render(page, DDJVU_RENDER_COLOR, &rect, &rect, fmt, unsigned(stride),
                                       reinterpret_cast<char*>(px));
                for (int y = 0; ok && y < H; ++y) {   // paper is opaque
                    auto* row = reinterpret_cast<std::uint32_t*>(px + std::size_t(y) * std::size_t(stride));
                    for (int x = 0; x < W; ++x) row[x] |= 0xff000000u;
                }
                cairo_surface_mark_dirty(s);
                ddjvu_format_release(fmt);
            }
            ddjvu_page_release(page);
        }
        cairo_save(cr);
        if (ok) {
            cairo_set_source_surface(cr, s, 0, 0);
            cairo_paint(cr);
        } else {
            cairo_rectangle(cr, 0, 0, W, H);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill(cr);
        }
        cairo_restore(cr);
        cairo_surface_destroy(s);
    }
    std::vector<core::search::Hit> find(int i, const std::string& text) const override {
        std::vector<core::search::Hit> out;
        for (const auto& r : core::words::find(words(i), text)) out.push_back({i, r});
        return out;
    }
    std::string text(int i) const override {
        std::string s;
        const auto& ws = words(i);
        for (std::size_t k = 0; k < ws.size(); ++k) {
            if (k) {
                const auto& a = ws[k - 1].box;
                const auto& b = ws[k].box;
                s += b.x < a.x || b.y > a.y + a.h * 0.6 ? "\n" : " ";
            }
            s += ws[k].text;
        }
        return s;
    }

private:
    // The page's words (read once): djvulibre's s-expression, pixels from the
    // bottom -> points from the top.
    const std::vector<core::words::Word>& words(int i) const {
        auto& slot = m_words.at(std::size_t(i));
        if (slot) return *slot;
        slot.emplace();
        miniexp_t e;
        while ((e = ddjvu_document_get_pagetext(m_doc, i, "word")) == miniexp_dummy) djvu_pump(m_ctx, true);
        const auto& p = m_pages[std::size_t(i)];
        const double k = 72.0 / p.dpi;
        // A text zone: (type xmin ymin xmax ymax child...) -- a word's child is its string.
        auto walk = [&](auto&& self, miniexp_t z) -> void {
            if (!miniexp_consp(z)) return;
            const miniexp_t type = miniexp_car(z);
            const bool word = type == miniexp_symbol("word");
            miniexp_t rest = miniexp_cdr(z);
            int c[4];
            for (int n = 0; n < 4; ++n) {
                c[n] = miniexp_numberp(miniexp_car(rest)) ? miniexp_to_int(miniexp_car(rest)) : 0;
                rest = miniexp_cdr(rest);
            }
            if (word && miniexp_stringp(miniexp_car(rest))) {
                core::words::Word w;
                w.text = miniexp_to_str(miniexp_car(rest));
                w.box = {c[0] * k, p.h - c[3] * k, (c[2] - c[0]) * k, (c[3] - c[1]) * k};
                slot->push_back(std::move(w));
                return;
            }
            for (; miniexp_consp(rest); rest = miniexp_cdr(rest)) self(self, miniexp_car(rest));
        };
        if (e != miniexp_nil) walk(walk, e);
        ddjvu_miniexp_release(m_doc, e);
        return *slot;
    }

    ddjvu_context_t*  m_ctx;
    ddjvu_document_t* m_doc;
    std::vector<PageInfo> m_pages;
    mutable std::vector<std::optional<std::vector<core::words::Word>>> m_words;
};

// ── CBZ ─────────────────────────────────────────────────────────────────────
class ComicDocument final : public Document {
public:
    struct Page { core::zip::Entry entry; int w = 0, h = 0; };
    ComicDocument(std::string zip, std::vector<Page> pages, std::string path)
        : m_zip(std::move(zip)), m_pages(std::move(pages)) {
        m_kind = core::Kind::Cbz;
        m_path = std::move(path);
    }
    int count() const override { return int(m_pages.size()); }
    core::layout::Size size(int i) const override {
        const auto& p = m_pages.at(std::size_t(i));
        return {double(p.w), double(p.h)};
    }
    void render(cairo_t* cr, int i, double scale, bool) const override {
        const auto sz = size(i);
        paint_picture(cr, picture(i), sz.w, sz.h, scale, false);
    }

private:
    cairo_surface_t* picture(int i) const {
        if (auto* s = m_cache.get(i)) return s;
        auto bytes = unpack(m_zip, m_pages.at(std::size_t(i)).entry);
        cairo_surface_t* s = bytes ? decode(*bytes) : nullptr;
        if (s) m_cache.put(i, s);
        return s;
    }
    std::string m_zip;
    std::vector<Page> m_pages;
    Pictures m_cache;
};

// ── EPUB ────────────────────────────────────────────────────────────────────
// A book page: 6 x 9 in, 0.75 in margins; 11 pt serif, headings 17 pt bold.
constexpr double kPageW = 432, kPageH = 648, kMargin = 54;

class EpubDocument final : public Document {
public:
    // chapter -1: the cover. top / bottom: the slice of the chapter's layout
    // (points); first / end: its text (bytes); lines [line0, line1).
    struct Page { int chapter = -1; double top = 0, bottom = 0; int first = 0, end = 0; int line0 = 0, line1 = 0; };

    EpubDocument(std::string zip, std::string title, std::string cover_path, std::vector<PangoLayout*> chapters,
                 std::vector<Page> pages, std::string path)
        : m_zip(std::move(zip)), m_cover(std::move(cover_path)), m_chapters(std::move(chapters)), m_pages(std::move(pages)) {
        m_kind = core::Kind::Epub;
        m_path = std::move(path);
        m_title = std::move(title);
    }
    ~EpubDocument() override { for (auto* l : m_chapters) g_object_unref(l); }
    int count() const override { return int(m_pages.size()); }
    core::layout::Size size(int) const override { return {kPageW, kPageH}; }
    bool searchable() const override { return true; }
    void render(cairo_t* cr, int i, double scale, bool) const override {
        const auto& p = m_pages.at(std::size_t(i));
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr, 0, 0, kPageW, kPageH);
        cairo_fill(cr);
        cairo_restore(cr);
        if (p.chapter < 0) {   // the cover, fitted to the page
            paint_picture(cr, cover(), kPageW, kPageH, scale, true);
            return;
        }
        // Only this page's lines, each at its baseline: a clip would let the
        // next line's ascenders (or the last one's descenders) peek in.
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        PangoLayoutIter* it = pango_layout_get_iter(m_chapters[std::size_t(p.chapter)]);
        int n = 0;
        do {
            if (n >= p.line1) break;
            if (n >= p.line0) {
                PangoRectangle log;
                pango_layout_iter_get_line_extents(it, nullptr, &log);
                const double base = pango_layout_iter_get_baseline(it) / double(PANGO_SCALE);
                cairo_move_to(cr, kMargin + log.x / double(PANGO_SCALE), kMargin + base - p.top);
                pango_cairo_show_layout_line(cr, pango_layout_iter_get_line_readonly(it));
            }
            ++n;
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
        cairo_restore(cr);
    }
    std::string text(int i) const override {
        const auto& p = m_pages.at(std::size_t(i));
        if (p.chapter < 0) return {};
        const char* t = pango_layout_get_text(m_chapters[std::size_t(p.chapter)]);
        return std::string(t + p.first, std::size_t(p.end - p.first));
    }
    std::vector<core::search::Hit> find(int i, const std::string& q) const override {
        std::vector<core::search::Hit> out;
        const auto& p = m_pages.at(std::size_t(i));
        if (p.chapter < 0 || q.empty()) return out;
        PangoLayout* l = m_chapters[std::size_t(p.chapter)];
        const std::string all = pango_layout_get_text(l);
        auto lower = [](std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; };
        const std::string hay = lower(all.substr(std::size_t(p.first), std::size_t(p.end - p.first)));
        const std::string needle = lower(q);
        for (std::size_t at = 0; (at = hay.find(needle, at)) != std::string::npos; at += needle.size()) {
            const int a = p.first + int(at), b = a + int(needle.size()) - 1;
            PangoRectangle ra, rb;
            pango_layout_index_to_pos(l, a, &ra);
            pango_layout_index_to_pos(l, b, &rb);
            auto rect = [&](double x0, double x1, const PangoRectangle& r) {
                return core::layout::Rect{kMargin + x0, kMargin + r.y / double(PANGO_SCALE) - p.top, x1 - x0,
                                          r.height / double(PANGO_SCALE)};
            };
            const double xa = ra.x / double(PANGO_SCALE), xb = (rb.x + rb.width) / double(PANGO_SCALE);
            if (ra.y == rb.y) out.push_back({i, rect(std::min(xa, xb), std::max(xa, xb), ra)});
            else {   // over a line break: the end of one line, the start of the next
                out.push_back({i, rect(xa, kPageW - 2 * kMargin, ra)});
                out.push_back({i, rect(0, xb, rb)});
            }
        }
        return out;
    }

private:
    cairo_surface_t* cover() const {
        if (auto* s = m_cache.get(0)) return s;
        std::string err;
        const auto es = core::zip::list(m_zip, err);
        const int k = core::zip::find(es, m_cover);
        if (k < 0) return nullptr;
        auto bytes = unpack(m_zip, es[std::size_t(k)]);
        cairo_surface_t* s = bytes ? decode(*bytes) : nullptr;
        if (s) m_cache.put(0, s);
        return s;
    }
    std::string m_zip, m_cover;
    std::vector<PangoLayout*> m_chapters;
    std::vector<Page> m_pages;
    Pictures m_cache;
};

PangoContext* book_context() {
    static PangoContext* ctx = [] {
        PangoFontMap* fm = pango_cairo_font_map_get_default();
        PangoContext* c = pango_font_map_create_context(fm);
        pango_cairo_context_set_resolution(c, 72.0);   // units are points
        cairo_font_options_t* fo = cairo_font_options_create();
        cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);   // the same layout at any zoom
        cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
        pango_cairo_context_set_font_options(c, fo);
        cairo_font_options_destroy(fo);
        return c;
    }();
    return ctx;
}

std::string escape(const std::string& s) {
    gchar* e = g_markup_escape_text(s.c_str(), gssize(s.size()));
    std::string out = e;
    g_free(e);
    return out;
}

}  // namespace

std::unique_ptr<Document> open_djvu(const std::string& path, std::string& error) {
    ddjvu_context_t* ctx = ddjvu_context_create("lookit");
    if (!ctx) { error = "DjVu: couldn't start the decoder."; return nullptr; }
    ddjvu_document_t* doc = ddjvu_document_create_by_filename_utf8(ctx, path.c_str(), TRUE);
    if (!doc) { ddjvu_context_release(ctx); error = "DjVu: couldn't read it."; return nullptr; }
    std::string why;
    while (!ddjvu_document_decoding_done(doc)) djvu_pump(ctx, true, &why);
    if (ddjvu_document_decoding_error(doc)) {
        error = "DjVu: couldn't read it" + (why.empty() ? std::string(".") : " (" + why + ").");
        ddjvu_document_release(doc);
        ddjvu_context_release(ctx);
        return nullptr;
    }
    const int n = ddjvu_document_get_pagenum(doc);
    std::vector<DjvuDocument::PageInfo> pages;
    for (int i = 0; i < n; ++i) {
        ddjvu_pageinfo_t info{};
        ddjvu_status_t r;
        while ((r = ddjvu_document_get_pageinfo(doc, i, &info)) < DDJVU_JOB_OK) djvu_pump(ctx, true);
        DjvuDocument::PageInfo p;
        if (r == DDJVU_JOB_OK && info.width > 0 && info.height > 0) {
            p.dpi = info.dpi > 0 ? info.dpi : 300;
            const bool side = info.rotation % 2;   // the page's own quarter turn
            p.pw = side ? info.height : info.width;
            p.ph = side ? info.width : info.height;
            p.w = p.pw * 72.0 / p.dpi;
            p.h = p.ph * 72.0 / p.dpi;
        }
        pages.push_back(p);
    }
    if (pages.empty()) {
        ddjvu_document_release(doc);
        ddjvu_context_release(ctx);
        error = "DjVu: no pages.";
        return nullptr;
    }
    return std::make_unique<DjvuDocument>(ctx, doc, std::move(pages), path);
}

std::unique_ptr<Document> open_cbz(const std::string& path, std::string& error) {
    std::string zip;
    if (!slurp(path, zip, error)) return nullptr;
    std::string why;
    const auto es = core::zip::list(zip, why);
    if (es.empty()) { error = "Comic: Lookit can't read this archive (" + why + ")."; return nullptr; }
    std::vector<ComicDocument::Page> pages;
    for (int k : core::comic::pages(es)) {
        ComicDocument::Page p{es[std::size_t(k)]};
        // The header gives the size; a deflated picture or an unknown kind is decoded.
        std::optional<std::pair<int, int>> sz;
        if (p.entry.method == 0)
            if (auto raw = core::zip::raw(zip, p.entry)) sz = core::comic::image_size(raw->substr(0, 65536));
        if (!sz)
            if (auto bytes = unpack(zip, p.entry)) {
                sz = core::comic::image_size(bytes->substr(0, 65536));
                if (!sz)
                    if (cairo_surface_t* s = decode(*bytes)) {
                        sz = std::pair{cairo_image_surface_get_width(s), cairo_image_surface_get_height(s)};
                        cairo_surface_destroy(s);
                    }
            }
        if (!sz) continue;   // not a picture GDK can read: not a page
        p.w = sz->first;
        p.h = sz->second;
        pages.push_back(std::move(p));
    }
    if (pages.empty()) { error = "Comic: there are no pictures in it."; return nullptr; }
    if (auto lg = log::get(log::Area::Io)) lg->info("comic: {} page(s)", pages.size());
    return std::make_unique<ComicDocument>(std::move(zip), std::move(pages), path);
}

std::unique_ptr<Document> open_epub(const std::string& path, std::string& error) {
    namespace E = core::epub;
    std::string zip;
    if (!slurp(path, zip, error)) return nullptr;
    std::string why;
    const auto es = core::zip::list(zip, why);
    if (es.empty()) { error = "EPUB: Lookit can't read this archive (" + why + ")."; return nullptr; }
    auto read = [&](const std::string& name) -> std::string {
        const int k = core::zip::find(es, name);
        if (k < 0) return {};
        auto b = unpack(zip, es[std::size_t(k)]);
        return b ? *b : std::string{};
    };
    const std::string opf_path = E::package_path(read("META-INF/container.xml"));
    const std::string opf = opf_path.empty() ? std::string{} : read(opf_path);
    if (opf.empty()) { error = "EPUB: the book's contents list is missing."; return nullptr; }
    const auto pkg = E::parse_package(opf, opf_path);

    using Page = EpubDocument::Page;
    std::vector<Page> pages;
    if (!pkg.cover.empty() && core::zip::find(es, pkg.cover) >= 0) pages.push_back({-1});
    std::vector<PangoLayout*> chapters;
    const double content_h = kPageH - 2 * kMargin;
    PangoFontDescription* font = pango_font_description_from_string("Serif 11");
    for (const auto& ch : pkg.spine) {
        const auto blocks = E::blocks(read(ch));
        if (blocks.empty()) continue;
        std::string markup;
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            if (b) markup += "\n\n";
            markup += blocks[b].heading ? "<span size=\"17pt\" weight=\"bold\">" + escape(blocks[b].text) + "</span>"
                                        : escape(blocks[b].text);
        }
        PangoLayout* l = pango_layout_new(book_context());
        pango_layout_set_font_description(l, font);
        pango_layout_set_width(l, int((kPageW - 2 * kMargin) * PANGO_SCALE));
        pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_markup(l, markup.c_str(), -1);
        const int chapter = int(chapters.size());
        chapters.push_back(l);
        // Pages: whole lines, as many as fit; a chapter starts a new page.
        Page cur{chapter, 0, 0, 0, 0, 0, 0};
        bool any = false;
        int n = 0;
        PangoLayoutIter* it = pango_layout_get_iter(l);
        do {
            PangoRectangle log;
            pango_layout_iter_get_line_extents(it, nullptr, &log);
            const double top = log.y / double(PANGO_SCALE), bottom = (log.y + log.height) / double(PANGO_SCALE);
            PangoLayoutLine* line = pango_layout_iter_get_line_readonly(it);
            if (any && bottom - cur.top > content_h) {
                pages.push_back(cur);
                cur = {chapter, top, top, line->start_index, line->start_index, n, n};
            }
            if (!any) { cur.top = top; cur.first = line->start_index; cur.line0 = n; any = true; }
            cur.bottom = bottom;
            cur.end = line->start_index + line->length;
            cur.line1 = ++n;
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
        if (any) pages.push_back(cur);
    }
    pango_font_description_free(font);
    if (pages.empty()) {
        for (auto* l : chapters) g_object_unref(l);
        error = "EPUB: there is nothing in it Lookit can show.";
        return nullptr;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("epub: {} chapter(s), {} page(s)", chapters.size(), pages.size());
    return std::make_unique<EpubDocument>(std::move(zip), pkg.title, pkg.cover, std::move(chapters), std::move(pages), path);
}

}  // namespace lookit::books
