// save_check -- does a signature land where you saw it? (s008)
//
// Headless, no display: makes a 3-page PDF (plain; /Rotate 90; an offset
// CropBox), signs every page at turns 0 / right / left, then compares the
// page as Lookit draws it before saving with the page poppler draws from the
// saved file. The signature's red and green halves must land within 2 px.
//
// s009: markup too -- highlights (multiplied: text under them stays dark),
// boxes, arrows (magenta -- the paper's blue rules shift a pixel on a turned
// page, an older quirk of the rotated render), text on the same pages; and a note must come out as a real
// /Text annotation where it was put.
//
//   ./build/lookit_savecheck            prints one line per page, then OK / FAIL
#include "Document.hpp"
#include "Save.hpp"

#include <cairo-pdf.h>
#include <glib.h>
#include <gtkmm/init.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>

using namespace lookit;
namespace fs = std::filesystem;

int main() {
    Gtk::init_gtkmm_internals();
    const fs::path dir = fs::temp_directory_path() / "lookit_savecheck";
    fs::create_directories(dir);
    const std::string base = (dir / "base.pdf").string(), src = (dir / "src.pdf").string(),
                      art = (dir / "art.png").string(), out = (dir / "out.pdf").string();
    {   // three Letter pages: a black mark top-left, blue rules
        auto* s = cairo_pdf_surface_create(base.c_str(), 612, 792);
        auto* cr = cairo_create(s);
        for (int p = 0; p < 3; ++p) {
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, 20, 20, 100, 40);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0, 0, 1);
            for (int x = 0; x < 612; x += 50) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, 792); }
            cairo_set_line_width(cr, 0.5);
            cairo_stroke(cr);
            cairo_show_page(cr);
        }
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    {   // page 2 turned by the file itself, page 3 cropped off-centre
        QPDF q;
        q.processFile(base.c_str());
        auto pages = QPDFPageDocumentHelper(q).getAllPages();
        pages[1].getObjectHandle().replaceKey("/Rotate", QPDFObjectHandle::newInteger(90));
        pages[2].getObjectHandle().replaceKey(
            "/CropBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(50, 100, 500, 700)));
        QPDFWriter w(q, src.c_str());
        w.write();
    }
    {   // the "signature": red left half, green right half -- so a wrong turn shows
        auto* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 300, 100);
        auto* cr = cairo_create(s);
        cairo_set_source_rgb(cr, 1, 0, 0);
        cairo_rectangle(cr, 0, 0, 150, 100);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0, 0.8, 0);
        cairo_rectangle(cr, 150, 0, 150, 100);
        cairo_fill(cr);
        cairo_surface_write_to_png(s, art.c_str());
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    std::string err;
    auto d = Document::open(src, err);
    if (!d) { std::cerr << "open: " << err << "\n"; return 2; }
    std::vector<core::PageRec> recs;
    for (int i = 0; i < 3; ++i)
        for (int turn : {0, 1, 3}) {
            core::PageRec r{0, i, turn, double(recs.size())};
            const auto sz = d->size(i);
            auto st = core::marks::signature(art, 300, 100, sz.w, sz.h, turn);
            r.marks.push_back(st);
            st.x = 10;
            st.y = 15;
            r.marks.push_back(st);
            recs.push_back(r);
        }
    auto before = Document::compose({src}, recs, out, err);
    if (!save::write_pdf({src}, recs, out, err)) { std::cerr << "save: " << err << "\n"; return 2; }
    Document::forget(out);
    auto after = Document::open(out, err);
    if (!after) { std::cerr << "reopen: " << err << "\n"; return 2; }
    int bad = 0;
    for (int i = 0; i < before->count(); ++i) {
        auto ta = render_texture(*before, i, 1.0), tb = render_texture(*after, i, 1.0, 16e6, false);
        const int w = ta->get_width(), h = ta->get_height();
        if (w != tb->get_width() || h != tb->get_height()) {
            std::cout << "page " << i + 1 << ": size differs\n";
            ++bad;
            continue;
        }
        std::vector<unsigned char> A(std::size_t(w * h * 4)), B(A.size());
        ta->download(A.data(), std::size_t(w) * 4);
        tb->download(B.data(), std::size_t(w) * 4);
        auto box = [&](const std::vector<unsigned char>& P, bool red) {
            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const auto* p = &P[std::size_t((y * w + x) * 4)];   // BGRA
                    const bool hit = red ? (p[2] > 200 && p[1] < 60 && p[0] < 60) : (p[1] > 150 && p[2] < 60 && p[0] < 60);
                    if (hit) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
                }
            return std::array<int, 4>{x0, y0, x1, y1};
        };
        int off = 0;
        for (bool red : {true, false}) {
            const auto a = box(A, red), b = box(B, red);
            for (int k = 0; k < 4; ++k) off = std::max(off, std::abs(a[std::size_t(k)] - b[std::size_t(k)]));
        }
        std::cout << "page " << i + 1 << " (" << w << "x" << h << "): signature within " << off << " px\n";
        if (off > 2) ++bad;
    }
    // ── s009: markup -- a highlight over the black mark (multiplied: the mark
    // must stay black), a red box, a magenta arrow, black text; each page at
    // turns 0 and right. Pixels are sorted into dark / yellow / red / arrow
    // and each class must cover the same pixels before and after (IoU >= 85%).
    std::vector<core::PageRec> recs2;
    namespace M = core::marks;
    for (int i = 0; i < 3; ++i)
        for (int turn : {0, 1}) {
            core::PageRec r{0, i, turn, double(recs2.size())};
            const auto sz = d->size(i);   // unturned seen size
            // the black mark sits top-left in the file's own frame; find it on this page
            auto probe = render_texture(*d, i, 1.0);
            const int pw = probe->get_width(), ph = probe->get_height();
            std::vector<unsigned char> px(std::size_t(pw * ph * 4));
            probe->download(px.data(), std::size_t(pw) * 4);
            int x0 = pw, y0 = ph, x1 = 0, y1 = 0;
            for (int y = 0; y < ph; ++y)
                for (int x = 0; x < pw; ++x) {
                    const auto* p = &px[std::size_t((y * pw + x) * 4)];
                    if (p[0] < 40 && p[1] < 40 && p[2] < 40) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
                }
            core::Mark hl = M::highlight({{double(x0 - 8), double(y0 - 6), double(x1 - x0 + 16), double(y1 - y0 + 12)}},
                                         {0, 0, 1, 1}, 0xffd400ffu, sz.w, sz.h, 0);
            core::Mark box = M::boxed(core::MarkKind::Box, {sz.w * 0.5, sz.h * 0.3, 120, 60}, 0xff0000ffu, 3, sz.w, sz.h, 0);
            core::Mark arrow = M::line(core::MarkKind::Arrow, {sz.w * 0.2, sz.h * 0.7}, {sz.w * 0.6, sz.h * 0.55},
                                       0xff00ffffu, 3, sz.w, sz.h, 0);   // magenta: the paper has none
            core::Mark text = M::boxed(core::MarkKind::Text, {sz.w * 0.1, sz.h * 0.85, 200, 30}, 0x000000ffu, 1,
                                       sz.w, sz.h, 0);
            text.text = "Signed and dated";
            text.size = 16;
            r.marks = {hl, box, arrow, text};
            recs2.push_back(r);
        }
    const std::string out2 = (dir / "out2.pdf").string();
    auto before2 = Document::compose({src}, recs2, out2, err);
    if (!save::write_pdf({src}, recs2, out2, err)) { std::cerr << "save 2: " << err << "\n"; return 2; }
    Document::forget(out2);
    auto after2 = Document::open(out2, err);
    if (!after2) { std::cerr << "reopen 2: " << err << "\n"; return 2; }
    auto cls = [](const unsigned char* p) {   // BGRA
        const int b = p[0], g = p[1], r = p[2];
        if (r < 70 && g < 70 && b < 70) return 1;             // dark
        if (r > 200 && g > 160 && b < 90) return 2;           // yellow
        if (r > 200 && g < 70 && b < 70) return 3;            // red
        if (r > 200 && b > 200 && g < 70) return 4;           // magenta (the arrow)
        return 0;
    };
    for (int i = 0; i < before2->count(); ++i) {
        auto ta = render_texture(*before2, i, 1.0), tb = render_texture(*after2, i, 1.0, 16e6, false);
        const int w = ta->get_width(), h = ta->get_height();
        if (w != tb->get_width() || h != tb->get_height()) { std::cout << "markup page " << i + 1 << ": size differs\n"; ++bad; continue; }
        std::vector<unsigned char> A(std::size_t(w * h * 4)), B(A.size());
        ta->download(A.data(), std::size_t(w) * 4);
        tb->download(B.data(), std::size_t(w) * 4);
        std::array<long, 5> both{}, either{};
        for (std::size_t k = 0; k < A.size(); k += 4) {
            const int ca = cls(&A[k]), cb = cls(&B[k]);
            for (int c = 1; c < 5; ++c) {
                both[std::size_t(c)] += (ca == c && cb == c);
                either[std::size_t(c)] += (ca == c || cb == c);
            }
        }
        std::cout << "markup page " << i + 1 << ":";
        bool ok = true;
        const char* names[] = {"", "dark", "yellow", "red", "arrow"};
        for (int c = 1; c < 5; ++c) {
            const double iou = either[std::size_t(c)] ? double(both[std::size_t(c)]) / either[std::size_t(c)] : 1.0;
            std::cout << " " << names[c] << " " << int(iou * 100) << "%";
            if (iou < 0.85) ok = false;
        }
        std::cout << (ok ? "" : "  <- off") << "\n";
        if (!ok) {   // keep the pair to look at
            ++bad;
            ta->save_to_png((dir / ("markup-" + std::to_string(i + 1) + "-screen.png")).string());
            tb->save_to_png((dir / ("markup-" + std::to_string(i + 1) + "-saved.png")).string());
            std::cout << "  (both renders in " << dir.string() << ")\n";
        }
    }
    // Notes: real annotations, at the right place.
    {
        core::PageRec r{0, 1, 0, 0};   // the page the file turns right
        const auto sz = d->size(1);
        r.marks = {M::note({100, 50}, "Please initial here", 0xffd400ffu, sz.w, sz.h, 0)};
        const std::string out3 = (dir / "out3.pdf").string();
        if (!save::write_pdf({src}, {r}, out3, err)) { std::cerr << "save 3: " << err << "\n"; return 2; }
        QPDF q;
        q.processFile(out3.c_str());
        auto pages = QPDFPageDocumentHelper(q).getAllPages();
        auto annots = pages[0].getObjectHandle().getKey("/Annots");
        bool ok = annots.isArray() && annots.getArrayNItems() == 1;
        if (ok) {
            auto a = annots.getArrayItem(0);
            auto rect = a.getKey("/Rect").getArrayAsRectangle();
            ok = a.getKey("/Subtype").getName() == "/Text" && a.getKey("/Contents").getUTF8Value() == "Please initial here" &&
                 std::abs(rect.llx - 50) < 0.01 && std::abs(rect.lly - 100) < 0.01;   // /Rotate 90: seen (100,50) -> pdf (50,100)
            std::cout << "note: /Text annotation \"" << a.getKey("/Contents").getUTF8Value() << "\" at "
                      << rect.llx << "," << rect.lly << (ok ? "" : "  <- off") << "\n";
        } else {
            std::cout << "note: no annotation written  <- off\n";
        }
        if (!ok) ++bad;
    }
    std::cout << (bad ? "FAIL" : "OK") << "\n";
    return bad ? 1 : 0;
}
