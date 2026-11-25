#include "Document.hpp"
#include "Books.hpp"
#include "Log.hpp"
#include "MarkArt.hpp"

#include <gdkmm/texture.h>
#include <giomm/file.h>
#include <glibmm/convert.h>
#include <glibmm/error.h>
#include <poppler.h>

#include <algorithm>
#include <filesystem>
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
    std::string path;
    std::map<int, std::string> original;   // form field id -> the file's own value (s011)
    ~Poppler() {
        for (auto* p : pages) if (p) g_object_unref(p);
        if (doc) g_object_unref(doc);
    }
};

std::map<std::string, std::weak_ptr<Poppler>>& poppler_pool() {
    static std::map<std::string, std::weak_ptr<Poppler>> pool;
    return pool;
}

std::map<std::string, std::string>& passwords() {
    static std::map<std::string, std::string> keep;
    return keep;
}

// ── form fields (s011) ──────────────────────────────────────────────────────
core::FieldKind field_kind(PopplerFormField* f) {
    using K = core::FieldKind;
    switch (poppler_form_field_get_field_type(f)) {
        case POPPLER_FORM_FIELD_TEXT: return K::Text;
        case POPPLER_FORM_FIELD_BUTTON:
            switch (poppler_form_field_button_get_button_type(f)) {
                case POPPLER_FORM_BUTTON_CHECK: return K::Check;
                case POPPLER_FORM_BUTTON_RADIO: return K::Radio;
                default: return K::Push;
            }
        case POPPLER_FORM_FIELD_CHOICE:
            return poppler_form_field_choice_get_choice_type(f) == POPPLER_FORM_CHOICE_COMBO ? K::Combo : K::List;
        case POPPLER_FORM_FIELD_SIGNATURE: return K::Signature;
        default: return K::Unknown;
    }
}

std::string take(gchar* s) {
    std::string out = s ? s : "";
    g_free(s);
    return out;
}

// The field's value in core::forms' encoding, as poppler holds it now.
std::string field_value(PopplerFormField* f) {
    namespace F = core::forms;
    switch (field_kind(f)) {
        case core::FieldKind::Text: return take(poppler_form_field_text_get_text(f));
        case core::FieldKind::Check:
        case core::FieldKind::Radio: return F::on(poppler_form_field_button_get_state(f));
        case core::FieldKind::Combo:
        case core::FieldKind::List: {
            std::vector<int> picks;
            for (int i = 0, n = poppler_form_field_choice_get_n_items(f); i < n; ++i)
                if (poppler_form_field_choice_is_item_selected(f, i)) picks.push_back(i);
            if (picks.empty() && poppler_form_field_choice_is_editable(f)) {
                const std::string t = take(poppler_form_field_choice_get_text(f));
                if (!t.empty()) return F::typed(t);
            }
            return F::picks(picks);
        }
        default: return {};
    }
}

void set_field_value(PopplerFormField* f, const std::string& v) {
    namespace F = core::forms;
    switch (field_kind(f)) {
        case core::FieldKind::Text: poppler_form_field_text_set_text(f, v.c_str()); break;
        case core::FieldKind::Check:
        case core::FieldKind::Radio: poppler_form_field_button_set_state(f, F::is_on(v)); break;
        case core::FieldKind::Combo:
        case core::FieldKind::List:
            poppler_form_field_choice_unselect_all(f);
            if (F::is_typed(v)) { poppler_form_field_choice_set_text(f, F::typed_words(v).c_str()); break; }
            for (int i : F::picked(v))
                if (i >= 0 && i < poppler_form_field_choice_get_n_items(f)) poppler_form_field_choice_select_item(f, i);
            break;
        default: break;
    }
}

// Every field mapping of every page, handed to `fn(page, mapping)`.
template <class Fn>
void each_field(const Poppler& p, Fn&& fn) {
    for (int i = 0; i < int(p.pages.size()); ++i) {
        if (!p.pages[std::size_t(i)]) continue;
        GList* maps = poppler_page_get_form_field_mapping(p.pages[std::size_t(i)]);
        for (GList* l = maps; l; l = l->next) fn(i, static_cast<PopplerFormFieldMapping*>(l->data));
        poppler_page_free_form_field_mapping(maps);
    }
}

// Make poppler's fields show `fields` (keys for source `src`); a field with no
// entry goes back to the file's own value. Only fields that differ are set
// (setting redraws the field's appearance). Radios and boxes turning ON go
// last, so a group's "off"s never undo its "on".
void apply_fields(Poppler& p, int src, const std::map<std::string, std::string>& fields) {
    std::vector<std::pair<PopplerFormField*, std::string>> later;
    std::vector<PopplerFormField*> held;
    each_field(p, [&](int, PopplerFormFieldMapping* m) {
        PopplerFormField* f = m->field;
        const int id = poppler_form_field_get_id(f);
        const auto it = fields.find(core::forms::key(src, id));
        std::string want;
        if (it != fields.end()) want = it->second;
        else if (auto o = p.original.find(id); o != p.original.end()) want = o->second;
        else return;
        if (field_value(f) == want) return;
        const auto k = field_kind(f);
        if ((k == core::FieldKind::Check || k == core::FieldKind::Radio) && core::forms::is_on(want)) {
            held.push_back(static_cast<PopplerFormField*>(g_object_ref(f)));
            later.push_back({held.back(), want});
        } else {
            set_field_value(f, want);
        }
    });
    for (auto& [f, v] : later) set_field_value(f, v);
    for (auto* f : held) g_object_unref(f);
}

std::shared_ptr<Poppler> load_poppler(const std::string& path, std::string& error) {
    auto& pool = poppler_pool();
    if (auto it = pool.find(path); it != pool.end())
        if (auto live = it->second.lock()) return live;
    GError* err = nullptr;
    std::string uri;
    try { uri = Glib::filename_to_uri(path); }
    catch (const Glib::Error& e) { error = e.what(); return nullptr; }
    const std::string pw = Document::password(path);
    PopplerDocument* doc = poppler_document_new_from_file(uri.c_str(), pw.empty() ? nullptr : pw.c_str(), &err);
    if (!doc) {
        if (err && err->domain == POPPLER_ERROR && err->code == POPPLER_ERROR_ENCRYPTED)
            error = Document::kLocked;
        else
            error = std::string("Poppler could not read it: ") + (err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return nullptr;
    }
    auto p = std::make_shared<Poppler>();
    p->doc = doc;
    const int n = poppler_document_get_n_pages(doc);
    for (int i = 0; i < n; ++i) p->pages.push_back(poppler_document_get_page(doc, i));
    p->path = path;
    each_field(*p, [&](int, PopplerFormFieldMapping* m) {
        p->original[poppler_form_field_get_id(m->field)] = field_value(m->field);
    });
    pool[path] = p;
    return p;
}

class PdfDocument final : public Document {
public:
    // `source`: the index in the page list's sources (core::Pages) -- the
    // first one, when one file was added twice (they share one poppler).
    struct Page { std::shared_ptr<Poppler> src; int index; int turn; std::vector<core::Mark> marks; int source = 0; };

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
    bool searchable() const override { return true; }
    std::vector<core::Mark> marks(int i) const override { return m_pages.at(std::size_t(i)).marks; }
    std::vector<core::Field> fields(int i) const override {
        std::vector<core::Field> out;
        const auto& pg = m_pages.at(std::size_t(i));
        PopplerPage* p = raw(pg);
        if (!p) return out;
        const double h = m_base[std::size_t(i)].h;
        GList* maps = poppler_page_get_form_field_mapping(p);
        for (GList* l = maps; l; l = l->next) {
            auto* m = static_cast<PopplerFormFieldMapping*>(l->data);
            PopplerFormField* f = m->field;
            core::Field fd;
            fd.src = pg.source;
            fd.id = poppler_form_field_get_id(f);
            fd.name = take(poppler_form_field_get_name(f));
            if (fd.name.empty()) fd.name = take(poppler_form_field_get_partial_name(f));
            if (fd.name.empty()) fd.name = "field " + std::to_string(fd.id & 0xffff);
            fd.kind = field_kind(f);
            const double x1 = std::min(m->area.x1, m->area.x2), x2 = std::max(m->area.x1, m->area.x2);
            const double y1 = std::min(m->area.y1, m->area.y2), y2 = std::max(m->area.y1, m->area.y2);
            fd.box = {x1, h - y2, x2 - x1, y2 - y1};   // PDF's bottom-up -> top-down
            fd.value = field_value(f);
            fd.read_only = poppler_form_field_is_read_only(f);
            if (fd.kind == core::FieldKind::Text) {
                fd.multiline = poppler_form_field_text_get_text_type(f) == POPPLER_FORM_TEXT_MULTILINE;
                fd.password = poppler_form_field_text_is_password(f);
                const int ml = poppler_form_field_text_get_max_len(f);
                fd.max_len = ml > 0 ? ml : -1;
            }
            if (fd.kind == core::FieldKind::Combo || fd.kind == core::FieldKind::List) {
                for (int k = 0, n = poppler_form_field_choice_get_n_items(f); k < n; ++k)
                    fd.options.push_back(take(poppler_form_field_choice_get_item(f, k)));
                fd.multi = poppler_form_field_choice_can_select_multiple(f);
                fd.editable = poppler_form_field_choice_is_editable(f);
            }
            out.push_back(std::move(fd));
        }
        poppler_page_free_form_field_mapping(maps);
        return out;
    }
    int turn(int i) const override { return m_pages.at(std::size_t(i)).turn; }
    core::layout::Size base(int i) const override { return m_base.at(std::size_t(i)); }
    std::vector<core::layout::Rect> text_rects(int i, core::marks::Point a, core::marks::Point b) const override {
        std::vector<core::layout::Rect> out;
        PopplerPage* p = raw(m_pages.at(std::size_t(i)));
        if (!p) return out;
        PopplerRectangle sel{a.x, a.y, b.x, b.y};
        cairo_region_t* reg = poppler_page_get_selected_region(p, 1.0, POPPLER_SELECTION_GLYPH, &sel);
        if (!reg) return out;
        for (int k = 0, n = cairo_region_num_rectangles(reg); k < n; ++k) {
            cairo_rectangle_int_t r;
            cairo_region_get_rectangle(reg, k, &r);
            out.push_back({double(r.x), double(r.y), double(r.width), double(r.height)});
        }
        cairo_region_destroy(reg);
        return out;
    }
    std::string text(int i) const override {
        PopplerPage* p = raw(m_pages.at(std::size_t(i)));
        if (!p) return {};
        gchar* t = poppler_page_get_text(p);
        std::string s = t ? t : "";
        g_free(t);
        return s;
    }
    void render(cairo_t* cr, int i, double scale, bool with_marks) const override {
        const auto& pg = m_pages.at(std::size_t(i));
        const auto b = m_base[std::size_t(i)];
        cairo_save(cr);
        cairo_scale(cr, scale, scale);
        // Quarter turns clockwise, about the page's own corner.
        mark_art::turn_frame(cr, pg.turn, b.w, b.h);
        cairo_rectangle(cr, 0, 0, b.w, b.h);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
        if (auto* p = raw(pg)) poppler_page_render(p, cr);
        if (with_marks)
            for (const auto& m : pg.marks) mark_art::draw(cr, m);
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
    cairo_surface_t* surface() const override { return m_surf; }
    void render(cairo_t* cr, int, double scale, bool) const override {
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
    apply_fields(*pop, 0, {});   // a pooled copy may still show an earlier fill: the file's own
    std::string title;
    if (gchar* t = poppler_document_get_title(pop->doc)) { title = t; g_free(t); }
    std::vector<PdfDocument::Page> pages;
    for (int i = 0; i < int(pop->pages.size()); ++i) pages.push_back({pop, i, 0, {}});
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

Glib::RefPtr<Gdk::Texture> render_texture(const Document& doc, int i, double s, double max_pixels, bool marks) {
    const auto sz = doc.size(i);
    double rs = s;
    if (sz.w * sz.h * rs * rs > max_pixels) rs = std::sqrt(max_pixels / (sz.w * sz.h));
    const int w = std::max(1, int(std::ceil(sz.w * rs)));
    const int h = std::max(1, int(std::ceil(sz.h * rs)));
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    doc.render(cr, i, rs, marks);
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

std::shared_ptr<Document> Document::picture(cairo_surface_t* surf, const std::string& path) {
    return std::make_shared<ImageDocument>(surf, cairo_image_surface_get_width(surf),
                                           cairo_image_surface_get_height(surf), path);
}

void Document::forget(const std::string& path) { poppler_pool().erase(path); }

void Document::remember_password(const std::string& path, const std::string& pw) {
    if (pw.empty()) passwords().erase(path);
    else passwords()[path] = pw;
}

std::string Document::password(const std::string& path) {
    const auto it = passwords().find(path);
    return it == passwords().end() ? std::string{} : it->second;
}

std::vector<std::pair<int, core::Field>> Document::all_fields() const {
    std::vector<std::pair<int, core::Field>> out;
    for (int i = 0; i < count(); ++i)
        for (auto& f : fields(i)) out.push_back({i, std::move(f)});
    return out;
}

std::vector<std::string> Document::filled_sources(const std::vector<std::string>& sources,
                                                  const std::map<std::string, std::string>& fields,
                                                  std::vector<std::string>& temps, std::string& error) {
    std::vector<std::string> out = sources;
    for (int i = 0; i < int(sources.size()); ++i) {
        const std::string pre = std::to_string(i) + ":";
        bool any = false;
        for (const auto& [k, v] : fields) any = any || k.rfind(pre, 0) == 0;
        if (!any) continue;
        std::string e;
        auto pop = load_poppler(sources[std::size_t(i)], e);
        if (!pop) { error = e; return {}; }
        apply_fields(*pop, i, fields);
        const std::string tmp = (std::filesystem::path(g_get_tmp_dir()) /
                                 ("lookit-form-" + std::to_string(g_get_real_time()) + "-" + std::to_string(i) + ".pdf"))
                                    .string();
        GError* err = nullptr;
        std::string uri;
        try { uri = Glib::filename_to_uri(tmp); } catch (const Glib::Error& x) { error = x.what(); return {}; }
        // Poppler writes the file as it was plus the filled fields (an
        // incremental update, still under the file's own password).
        if (!poppler_document_save(pop->doc, uri.c_str(), &err)) {
            error = std::string("couldn't write the filled form: ") + (err ? err->message : "unknown error");
            if (err) g_error_free(err);
            return {};
        }
        temps.push_back(tmp);
        remember_password(tmp, password(sources[std::size_t(i)]));
        out[std::size_t(i)] = tmp;
    }
    return out;
}

std::shared_ptr<Document> Document::compose(const std::vector<std::string>& sources,
                                            const std::vector<core::PageRec>& recs, const std::string& path,
                                            std::string& error, const std::map<std::string, std::string>& fields) {
    std::vector<std::shared_ptr<Poppler>> pops;
    std::vector<int> first;   // the first source sharing each one's poppler
    for (const auto& s : sources) {
        std::string e;
        pops.push_back(load_poppler(s, e));   // a source that fails gives blank pages, not a crash
        if (!pops.back() && error.empty()) error = e;
        int f = int(first.size());
        for (int k = 0; k < int(first.size()); ++k)
            if (pops.back() && pops[std::size_t(k)] == pops.back()) { f = k; break; }
        first.push_back(f);
        if (pops.back() && f == int(first.size()) - 1) apply_fields(*pops.back(), f, fields);
    }
    std::vector<PdfDocument::Page> pages;
    for (const auto& r : recs) {
        const bool ok = r.src >= 0 && r.src < int(pops.size());
        pages.push_back({ok ? pops[std::size_t(r.src)] : nullptr, r.index, r.turn, r.marks,
                         ok ? first[std::size_t(r.src)] : r.src});
    }
    return std::make_shared<PdfDocument>(std::move(pages), path, "");
}

std::unique_ptr<Document> Document::open(const std::string& path, std::string& error) {
    const core::Kind k = core::detect(path);
    if (auto lg = log::get(log::Area::Io)) lg->info("open {} as {}", path, core::kind_label(k));
    switch (k) {
        case core::Kind::Pdf:   return open_pdf(path, error);
        case core::Kind::Image: return open_image(path, error);
        case core::Kind::DjVu:  return books::open_djvu(path, error);   // s012 (Books.cpp)
        case core::Kind::Epub:  return books::open_epub(path, error);
        case core::Kind::Cbz:   return books::open_cbz(path, error);
        default:
            error = "Lookit doesn't know what kind of file this is.";
            return nullptr;
    }
}

}  // namespace lookit
