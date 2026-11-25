#include "Save.hpp"
#include "Log.hpp"
#include "MarkArt.hpp"
#include "Document.hpp"

#include <cairo-pdf.h>
#include <cairo.h>
#include <gdkmm/texture.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <map>
#include <set>
#include <memory>

namespace fs = std::filesystem;

namespace lookit::save {

namespace {

// The page as a reader sees it before any turn of ours: its crop box, sides
// swapped when the file itself turns it a quarter (/Rotate). That is the frame
// poppler shows, so it is the frame a stamp's box is in.
std::pair<double, double> seen_size(QPDFPageObjectHelper& page, int& rotate) {
    const auto box = page.getCropBox().getArrayAsRectangle();
    const double w = std::abs(box.urx - box.llx), h = std::abs(box.ury - box.lly);
    rotate = 0;
    auto r = page.getAttribute("/Rotate", false);
    if (r.isInteger()) rotate = int((r.getIntValue() % 360 + 360) % 360);
    return rotate % 180 ? std::pair{h, w} : std::pair{w, h};
}

// Marks onto `page`: drawn with Cairo into a one-page PDF the page's size
// (the overlay), which qpdf turns into a form XObject and places over the page
// -- the same machinery as `qpdf --overlay`. The page's own content is wrapped
// in q / Q first so nothing it leaves set (a clip, a matrix) bends ours.
// The overlay QPDF must outlive the write (its streams are copied lazily), so
// it goes into `keep`.
void stamp_page(QPDF& out, QPDFPageObjectHelper page, const std::vector<core::Mark>& marks,
                std::vector<std::shared_ptr<QPDF>>& keep, std::vector<std::string>& temps) {
    int rotate = 0;
    const auto [w, h] = seen_size(page, rotate);
    bool drawn = false;
    for (const auto& m : marks) drawn = drawn || m.kind != core::MarkKind::Note;
    if (!drawn) return;   // notes only: they go in as annotations (note_page)
    const std::string ov = (fs::path(g_get_tmp_dir()) /
                            ("lookit-stamp-" + std::to_string(g_get_real_time()) + "-" +
                             std::to_string(temps.size()) + ".pdf")).string();
    temps.push_back(ov);
    cairo_surface_t* pdf = cairo_pdf_surface_create(ov.c_str(), w, h);
    cairo_t* cr = cairo_create(pdf);
    for (const auto& m : marks) mark_art::draw(cr, m, mark_art::Notes::Skip, mark_art::Blend::Multiply);
    cairo_destroy(cr);
    cairo_surface_finish(pdf);
    const bool ok = cairo_surface_status(pdf) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(pdf);
    if (!ok) throw std::runtime_error("couldn't draw the signature");

    auto q = std::make_shared<QPDF>();
    q->setSuppressWarnings(true);   // cairo writes a harmless duplicate key
    q->processFile(ov.c_str());
    keep.push_back(q);
    auto ovpage = QPDFPageDocumentHelper(*q).getAllPages().at(0);
    QPDFObjectHandle fo = out.copyForeignObject(ovpage.getFormXObjectForPage());
    // Cairo makes the overlay page a transparency GROUP, and a group's blend
    // happens inside it, onto nothing: a "multiply" highlight would come out
    // as solid yellow over the text. Without the group the overlay's content
    // blends straight onto the page, as it does on screen.
    if (fo.isStream()) fo.getDict().removeKey("/Group");

    // Resources: this page's own copy (a shallow page copy may share them).
    QPDFObjectHandle res = page.getAttribute("/Resources", true);
    if (!res.isDictionary()) {
        res = QPDFObjectHandle::newDictionary();
        page.getObjectHandle().replaceKey("/Resources", res);
    }
    QPDFObjectHandle xo = res.getKey("/XObject");
    xo = xo.isDictionary() ? xo.shallowCopy() : QPDFObjectHandle::newDictionary();
    res.replaceKey("/XObject", xo);
    int suffix = 0;
    const std::string name = res.getUniqueResourceName("/LookitSig", suffix);
    xo.replaceKey(name, fo);
    // invert_transformations: undo the file's own /Rotate so the overlay lands
    // in the frame the reader saw (and poppler drew).
    const std::string place =
        page.placeFormXObject(fo, name, page.getCropBox().getArrayAsRectangle(), true, true, true);
    page.addPageContents(QPDFObjectHandle::newStream(&out, "q\n"), true);
    page.addPageContents(QPDFObjectHandle::newStream(&out, "\nQ\nq\n" + place + "\nQ\n"), false);
}

// Sticky notes as real PDF annotations (/Subtype /Text): every reader shows
// the icon and opens the text. The note's corner is mapped from the seen
// frame into PDF user space (the crop box, the file's own /Rotate undone).
void note_page(QPDF& out, QPDFPageObjectHelper page, const std::vector<core::Mark>& marks) {
    int rotate = 0;
    seen_size(page, rotate);
    const auto box = page.getCropBox().getArrayAsRectangle();
    QPDFObjectHandle annots;
    for (const auto& m : marks) {
        if (m.kind != core::MarkKind::Note) continue;
        if (!annots.isInitialized()) {
            annots = page.getObjectHandle().getKey("/Annots");
            annots = annots.isArray() ? annots.shallowCopy() : QPDFObjectHandle::newArray();
            page.getObjectHandle().replaceKey("/Annots", annots);
        }
        namespace M = core::marks;
        const auto a = M::to_pdf({m.x, m.y}, box.llx, box.lly, box.urx, box.ury, rotate);
        const auto b = M::to_pdf({m.x + m.w, m.y + m.h}, box.llx, box.lly, box.urx, box.ury, rotate);
        auto dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/Text"));
        dict.replaceKey("/Rect", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
                                     std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y))));
        dict.replaceKey("/Contents", QPDFObjectHandle::newUnicodeString(m.text));
        dict.replaceKey("/Name", QPDFObjectHandle::newName("/Comment"));
        dict.replaceKey("/F", QPDFObjectHandle::newInteger(4 | 8 | 16));   // print, no zoom, no rotate
        dict.replaceKey("/C", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{
                                  QPDFObjectHandle::newReal(((m.color >> 24) & 0xff) / 255.0, 3),
                                  QPDFObjectHandle::newReal(((m.color >> 16) & 0xff) / 255.0, 3),
                                  QPDFObjectHandle::newReal(((m.color >> 8) & 0xff) / 255.0, 3)}));
        dict.replaceKey("/T", QPDFObjectHandle::newUnicodeString("Lookit"));
        dict.replaceKey("/P", page.getObjectHandle());
        annots.appendItem(out.makeIndirectObject(dict));
    }
}

}  // namespace

bool write_pdf(const std::vector<std::string>& sources, const std::vector<core::PageRec>& recs,
               const std::string& out_path, std::string& error, const core::Lock& lock) {
    const std::string tmp = out_path + ".lookit-tmp";
    try {
        // Sources may be the very file we're writing over: they're read fully
        // into qpdf before the rename, and the rename happens last.
        std::map<int, std::shared_ptr<QPDF>> open;
        std::vector<std::shared_ptr<QPDF>> overlays;   // alive until the write
        std::vector<std::string> temps;
        struct Gone {   // the overlay files go, however we leave
            std::vector<std::string>& t;
            ~Gone() { for (const auto& f : t) { std::error_code e; fs::remove(f, e); } }
        } gone{temps};
        std::map<int, std::vector<QPDFPageObjectHelper>> pages;
        std::map<int, std::shared_ptr<QPDFAcroFormDocumentHelper>> forms;
        auto load = [&](int src) {
            if (open.count(src)) return;
            auto q = std::make_shared<QPDF>();
            const std::string& path = sources.at(std::size_t(src));
            const std::string pw = Document::password(path);
            q->processFile(path.c_str(), pw.empty() ? nullptr : pw.c_str());
            pages[src] = QPDFPageDocumentHelper(*q).getAllPages();
            forms[src] = std::make_shared<QPDFAcroFormDocumentHelper>(*q);
            open[src] = q;
        };
        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper outdoc(out);
        QPDFAcroFormDocumentHelper outform(out);
        int fields = 0;
        for (const auto& r : recs) {
            load(r.src);
            auto& from = pages[r.src];
            if (r.index < 0 || r.index >= int(from.size())) continue;
            // A page used twice needs its own copy, so always add a shallow copy.
            QPDFPageObjectHelper page = from[std::size_t(r.index)];
            outdoc.addPage(page.shallowCopyPage(), false);
            auto added = outdoc.getAllPages().back();
            // Form fields (s011): a copied page keeps its widgets, but the
            // fields live in the document's /AcroForm -- carry them over (a
            // page used twice gets its own copies, renamed if they clash).
            if (forms[r.src]->hasAcroForm()) {
                std::set<QPDFObjGen> made;
                outform.fixCopiedAnnotations(added.getObjectHandle(), page.getObjectHandle(), *forms[r.src], &made);
                fields += int(made.size());
            }
            if (r.marks.empty() && !r.turn) continue;
            // Marks first: they're placed in the page's frame before our turn.
            if (!r.marks.empty()) {
                stamp_page(out, added, r.marks, overlays, temps);
                note_page(out, added, r.marks);
            }
            if (r.turn) added.rotatePage(90 * r.turn, true);
        }
        if (fields) {
            // The source's form-wide defaults (fonts, default look) come along
            // so a reader can redraw a field it edits.
            load(recs.empty() ? 0 : recs.front().src);
            auto af = open[recs.empty() ? 0 : recs.front().src]->getRoot().getKey("/AcroForm");
            auto oaf = out.getRoot().getKey("/AcroForm");
            if (af.isDictionary() && oaf.isDictionary())
            {   // plain values, made anew in `out` (a handle can't cross files)
                if (af.getKey("/DA").isString() && !oaf.hasKey("/DA"))
                    oaf.replaceKey("/DA", QPDFObjectHandle::newString(af.getKey("/DA").getStringValue()));
                if (af.getKey("/Q").isInteger() && !oaf.hasKey("/Q"))
                    oaf.replaceKey("/Q", QPDFObjectHandle::newInteger(af.getKey("/Q").getIntValue()));
            }
        }
        QPDFWriter w(out, tmp.c_str());
        w.setStaticID(false);
        using Mode = core::Lock::Mode;
        if (lock.mode == Mode::Password) {
            w.setR6EncryptionParameters(lock.password.c_str(), lock.password.c_str(), true, true, true, true, true,
                                        true, qpdf_r3p_full, true);
        } else if (lock.mode == Mode::Keep && !sources.empty()) {
            load(0);
            if (open[0]->isEncrypted()) w.copyEncryptionParameters(*open[0]);
        }
        w.write();
        if (auto lg = log::get(log::Area::Io); lg && !overlays.empty())
            lg->info("stamped {} page(s)", overlays.size());
        if (auto lg = log::get(log::Area::Io); lg && fields)
            lg->info("carried {} form field(s)", fields);
        if (auto lg = log::get(log::Area::Io); lg && lock.mode != Mode::Keep)   // never the password itself
            lg->info("lock: {}", lock.mode == Mode::None ? "none" : "password (AES-256)");
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove(tmp, ec);
        error = e.what();
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, out_path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        error = "Couldn't put the saved file in place: " + ec.message();
        return false;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("saved {} page(s) -> {}", recs.size(), out_path);
    return true;
}

std::string picture_to_pdf(const std::string& picture, std::string& error) {
    Glib::RefPtr<Gdk::Texture> tex;
    try { tex = Gdk::Texture::create_from_file(Gio::File::create_for_path(picture)); }
    catch (const Glib::Error& e) { error = e.what(); return ""; }
    const int w = tex->get_width(), h = tex->get_height();
    cairo_surface_t* img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(img);
    tex->download(cairo_image_surface_get_data(img), std::size_t(cairo_image_surface_get_stride(img)));
    cairo_surface_mark_dirty(img);

    const fs::path dir = fs::path(Glib::get_user_cache_dir()) / "lookit" / "pictures";
    std::error_code ec;
    fs::create_directories(dir, ec);
    static int serial = 0;
    const std::string out = (dir / (fs::path(picture).stem().string() + "-" +
                                    std::to_string(g_get_real_time()) + "-" + std::to_string(++serial) + ".pdf"))
                                .string();
    // 150 dpi: a phone photo becomes a page of sensible size, still sharp.
    const double k = 72.0 / 150.0;
    cairo_surface_t* pdf = cairo_pdf_surface_create(out.c_str(), w * k, h * k);
    cairo_t* cr = cairo_create(pdf);
    cairo_scale(cr, k, k);
    cairo_set_source_surface(cr, img, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_finish(pdf);
    const bool ok = cairo_surface_status(pdf) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(pdf);
    cairo_surface_destroy(img);
    if (!ok) { error = "Couldn't make a PDF page from the picture."; return ""; }
    return out;
}

}  // namespace lookit::save
