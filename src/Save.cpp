#include "Save.hpp"
#include "Log.hpp"

#include <cairo-pdf.h>
#include <cairo.h>
#include <gdkmm/texture.h>
#include <giomm/file.h>
#include <glibmm/miscutils.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>
#include <map>
#include <memory>

namespace fs = std::filesystem;

namespace lookit::save {

bool write_pdf(const std::vector<std::string>& sources, const std::vector<core::PageRec>& recs,
               const std::string& out_path, std::string& error) {
    const std::string tmp = out_path + ".lookit-tmp";
    try {
        // Sources may be the very file we're writing over: they're read fully
        // into qpdf before the rename, and the rename happens last.
        std::map<int, std::shared_ptr<QPDF>> open;
        std::map<int, std::vector<QPDFPageObjectHelper>> pages;
        QPDF out;
        out.emptyPDF();
        QPDFPageDocumentHelper outdoc(out);
        for (const auto& r : recs) {
            if (!open.count(r.src)) {
                auto q = std::make_shared<QPDF>();
                q->processFile(sources.at(std::size_t(r.src)).c_str());
                pages[r.src] = QPDFPageDocumentHelper(*q).getAllPages();
                open[r.src] = q;
            }
            auto& from = pages[r.src];
            if (r.index < 0 || r.index >= int(from.size())) continue;
            // A page used twice needs its own copy, so always add a shallow copy.
            QPDFPageObjectHelper page = from[std::size_t(r.index)];
            outdoc.addPage(page.shallowCopyPage(), false);
            if (r.turn) outdoc.getAllPages().back().rotatePage(90 * r.turn, true);
        }
        QPDFWriter w(out, tmp.c_str());
        w.setStaticID(false);
        w.write();
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
