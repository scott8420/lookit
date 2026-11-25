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
// s010: the picture tools -- every orientation, with and without a crop, is
// written as a PNG by Save's own path and read back: each pixel must be the
// source pixel the edit says it is (exact). Resize, light and JPEG too.
//
// s011: forms and passwords -- a locked one-page form opens only with its
// password; filled through the real verb, saved keeping the lock / open / with
// a new password, it comes back with every value, still a form, and drawn as
// Lookit drew it before the save.
//
//   ./build/lookit_savecheck            prints one line per page, then OK / FAIL
#include "Document.hpp"
#include "Export.hpp"
#include "PictureArt.hpp"
#include "Save.hpp"
#include "core/Picture.hpp"
#include "core/Forms.hpp"

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
    // Pictures (s010): each pixel's colour says where it came from.
    {
        namespace PI = core::picture;
        const int W = 40, H = 20;
        auto* src_s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
        cairo_surface_flush(src_s);
        auto px = [](cairo_surface_t* s, int x, int y) {
            return reinterpret_cast<const std::uint32_t*>(cairo_image_surface_get_data(s) +
                                                          std::size_t(y) * std::size_t(cairo_image_surface_get_stride(s)))[x];
        };
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                reinterpret_cast<std::uint32_t*>(cairo_image_surface_get_data(src_s) +
                                                 std::size_t(y) * std::size_t(cairo_image_surface_get_stride(src_s)))[x] =
                    0xff000000u | std::uint32_t(x * 6) << 16 | std::uint32_t(y * 12) << 8 | 200u;
        cairo_surface_mark_dirty(src_s);
        const std::string pic = (dir / "pic.png").string();
        cairo_surface_write_to_png(src_s, pic.c_str());
        auto orig = Document::open(pic, err);
        if (!orig || !orig->surface()) { std::cerr << "picture open: " << err << "\n"; return 2; }
        // Save's path: PictureArt -> a picture document -> exporter -> the file -> open again.
        auto roundtrip = [&](const core::PicEdit& e, exporter::Format f, const std::string& name) {
            auto doc = Document::picture(picture_art::apply(orig->surface(), e), pic);
            const std::string out = (dir / name).string();
            std::string e2;
            if (!exporter::write_page(*doc, 0, 72, f, out, e2)) { std::cerr << "picture save: " << e2 << "\n"; return std::shared_ptr<Document>(); }
            return std::shared_ptr<Document>(Document::open(out, e2));
        };
        int checked = 0, wrong = 0;
        for (int t = 0; t < 4; ++t)
            for (int f = 0; f < 2; ++f)
                for (bool crop : {false, true}) {
                    core::PicEdit e;
                    e.turn = t;
                    e.flip = f;
                    if (crop) e = PI::cropped(e, {3, 2, 11, 7}, W, H);
                    auto back = roundtrip(e, exporter::Format::Png, "pic-out.png");
                    if (!back) return 2;
                    const auto [ow, oh] = PI::out_size(e, W, H);
                    const auto c = PI::crop_of(e, W, H);
                    bool ok = back->size(0).w == ow && back->size(0).h == oh;
                    int seen = 0;
                    for (int y = 0; ok && y < H; ++y)
                        for (int x = 0; x < W; ++x) {
                            const auto o = PI::orient_point({x + 0.5, y + 0.5}, e, W, H);
                            const int u = int(std::floor(o.x - c.x)), v = int(std::floor(o.y - c.y));
                            if (u < 0 || v < 0 || u >= ow || v >= oh) continue;
                            ++seen;
                            if (px(back->surface(), u, v) != px(src_s, x, y)) { ok = false; break; }
                        }
                    ok = ok && seen == ow * oh;
                    ++checked;
                    if (!ok) {
                        ++wrong;
                        std::cout << "picture: turn " << t << " flip " << f << (crop ? " cropped" : "") << "  <- off\n";
                    }
                }
        std::cout << "picture: " << checked - wrong << " of " << checked << " orientations / crops exact through a saved PNG\n";
        if (wrong) ++bad;
        {   // resize: half size, the gradient still runs the right way
            core::PicEdit e = PI::resized({}, 20, 10, W, H);
            auto back = roundtrip(e, exporter::Format::Png, "pic-half.png");
            const bool ok = back && back->size(0).w == 20 && back->size(0).h == 10 &&
                            ((px(back->surface(), 19, 0) >> 16) & 255) > ((px(back->surface(), 0, 0) >> 16) & 255) + 150 &&
                            ((px(back->surface(), 0, 9) >> 8) & 255) > ((px(back->surface(), 0, 0) >> 8) & 255) + 150;
            std::cout << "picture: resized to 20 x 10" << (ok ? "" : "  <- off") << "\n";
            if (!ok) ++bad;
        }
        {   // light: brighter is brighter; JPEG keeps the size and roughly the colours
            core::PicEdit e = PI::with_light({}, 40, 0, 0);
            auto back = roundtrip(e, exporter::Format::Jpeg, "pic-light.jpg");
            bool ok = back && back->size(0).w == W && back->size(0).h == H;
            if (ok) {
                const auto a = px(src_s, 10, 10), b = px(back->surface(), 10, 10);
                ok = (b & 255) >= std::min(255u, (a & 255) + 30) - 12 && ((b >> 8) & 255) > ((a >> 8) & 255) + 30;
            }
            std::cout << "picture: lighter, through a JPEG" << (ok ? "" : "  <- off") << "\n";
            if (!ok) ++bad;
        }
        cairo_surface_destroy(src_s);
    }
    // ── s011: forms and passwords ───────────────────────────────────────────
    // A one-page form (a text field, a check box, a two-button radio group, a
    // drop-down, a list), locked with a password. It must refuse to open
    // without the password; fill through the real verb and compose; save
    // (keeping the lock, opening it, a new password); and come back with the
    // values, the fields still a form (/AcroForm), and the page looking as it
    // did in Lookit before the save.
    {
        namespace F = core::forms;
        const std::string form = (dir / "form.pdf").string(), locked = (dir / "form-locked.pdf").string();
        {
            static const char raw[] = R"PDF(%PDF-1.7
1 0 obj << /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R 6 0 R 7 0 R 10 0 R 11 0 R] /DA (/Helv 0 Tf 0 g) /DR << /Font << /Helv 20 0 R >> >> >> >> endobj
2 0 obj << /Type /Pages /Kids [3 0 R] /Count 1 >> endobj
3 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Resources << /Font << /F1 20 0 R >> >> /Annots [5 0 R 6 0 R 8 0 R 9 0 R 10 0 R 11 0 R] >> endobj
4 0 obj << >> stream
BT /F1 14 Tf 72 720 Td (Lookit form test) Tj ET
BT /F1 11 Tf 72 682 Td (Name) Tj ET
BT /F1 11 Tf 72 642 Td (Agree) Tj ET
BT /F1 11 Tf 72 602 Td (Size: small / large) Tj ET
BT /F1 11 Tf 72 562 Td (Colour) Tj ET
BT /F1 11 Tf 72 522 Td (Fruit) Tj ET
endstream endobj
5 0 obj << /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [150 675 400 695] /P 3 0 R /F 4 /DA (/Helv 12 Tf 0 g) /V (Ann) /MK << /BC [0 0 0] >> >> endobj
6 0 obj << /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /Rect [150 637 166 653] /P 3 0 R /F 4 /V /Off /AS /Off /MK << /BC [0 0 0] /CA (4) >> /DA (/ZaDb 0 Tf 0 g) /AP << /N << /Yes 12 0 R /Off 13 0 R >> >> >> endobj
7 0 obj << /FT /Btn /Ff 49152 /T (size) /V /large /Kids [8 0 R 9 0 R] >> endobj
8 0 obj << /Type /Annot /Subtype /Widget /Parent 7 0 R /Rect [250 597 266 613] /P 3 0 R /F 4 /AS /Off /MK << /BC [0 0 0] /CA (l) >> /AP << /N << /small 14 0 R /Off 13 0 R >> >> >> endobj
9 0 obj << /Type /Annot /Subtype /Widget /Parent 7 0 R /Rect [300 597 316 613] /P 3 0 R /F 4 /AS /large /MK << /BC [0 0 0] /CA (l) >> /AP << /N << /large 14 0 R /Off 13 0 R >> >> >> endobj
10 0 obj << /Type /Annot /Subtype /Widget /FT /Ch /Ff 131072 /T (colour) /Rect [150 555 300 575] /P 3 0 R /F 4 /DA (/Helv 12 Tf 0 g) /Opt [(Red) (Green) (Blue)] /V (Green) /MK << /BC [0 0 0] >> >> endobj
11 0 obj << /Type /Annot /Subtype /Widget /FT /Ch /T (fruit) /Rect [150 480 300 535] /P 3 0 R /F 4 /DA (/Helv 12 Tf 0 g) /Opt [(Apple) (Pear) (Plum)] /MK << /BC [0 0 0] >> >> endobj
12 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 16 16] >> stream
0 0 0 rg 3 3 10 10 re f
endstream endobj
13 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 16 16] >> stream
endstream endobj
14 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 16 16] >> stream
0 0 0 rg 4 4 8 8 re f
endstream endobj
20 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >> endobj
trailer << /Root 1 0 R /Size 21 >>
%%EOF
)PDF";
            // Hand-written: no xref, no lengths -- qpdf rebuilds them.
            QPDF q;
            q.setSuppressWarnings(true);
            q.processMemoryFile("form", raw, sizeof(raw) - 1);
            { QPDFWriter w(q, form.c_str()); w.write(); }
            QPDFWriter w(q, locked.c_str());
            w.setR6EncryptionParameters("s3cret", "s3cret", true, true, true, true, true, true, qpdf_r3p_full, true);
            w.write();
        }
        auto opens = [](const std::string& path, const std::string& pw, std::string& err) {
            Document::forget(path);
            Document::remember_password(path, pw);
            err.clear();
            return Document::open(path, err);
        };
        auto acro_fields = [](const std::string& path, const std::string& pw) {
            QPDF q;
            q.setSuppressWarnings(true);
            q.processFile(path.c_str(), pw.empty() ? nullptr : pw.c_str());
            auto af = q.getRoot().getKey("/AcroForm");
            return af.isDictionary() && af.getKey("/Fields").isArray() ? af.getKey("/Fields").getArrayNItems() : -1;
        };
        auto paint = [](const Document& d) {
            auto* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 612, 792);
            auto* cr = cairo_create(s);
            d.render(cr, 0, 1.0);
            cairo_destroy(cr);
            cairo_surface_flush(s);
            return s;
        };
        // Pixels that differ by more than a little (anti-aliasing allowed).
        auto differ = [](cairo_surface_t* a, cairo_surface_t* b) {
            int n = 0;
            const int st = cairo_image_surface_get_stride(a);
            const auto* pa = cairo_image_surface_get_data(a);
            const auto* pb = cairo_image_surface_get_data(b);
            for (int y = 0; y < 792; ++y)
                for (int x = 0; x < 612 * 4; ++x)
                    if (std::abs(int(pa[y * st + x]) - int(pb[y * st + x])) > 96) { ++n; break; }
            return n;   // rows with a difference
        };
        auto line = [&](const std::string& what, bool ok) {
            std::cout << "forms: " << what << (ok ? "" : "  <- off") << "\n";
            if (!ok) ++bad;
        };

        std::string err;
        line("a locked PDF asks for its password", !opens(locked, "", err) && err == Document::kLocked);
        line("the wrong password is refused", !opens(locked, "guess", err) && err == Document::kLocked);
        auto doc = opens(locked, "s3cret", err);
        const auto fs0 = doc ? doc->fields(0) : std::vector<core::Field>{};
        line("the right one opens it: 6 fields (a radio is two)", doc && fs0.size() == 6);

        // As the file came (the pool's poppler is shared: paint it before the fill).
        auto* blank = doc ? paint(*doc) : nullptr;
        // Fill it the way the Shell does: the verb, then compose.
        core::Pages pages;
        core::PageJournal j{pages};
        pages.set_door([&] { return j.open(); });
        pages.load(locked, 1);
        auto find = [&](const std::string& name, int nth = 0) {
            for (const auto& f : fs0) if (f.name == name && nth-- == 0) return f;
            return core::Field{};
        };
        bool filled = F::set(j, pages, find("name"), fs0, "Bob Smithers") &&
                      F::set(j, pages, find("agree"), fs0, F::on(true)) &&
                      F::set(j, pages, find("colour"), fs0, F::picks({2})) &&
                      F::set(j, pages, find("fruit"), fs0, F::picks({1}));
        // The radio that is off now ("small", left of "large").
        core::Field small = find("size", 0), large = find("size", 1);
        if (small.box.x > large.box.x) std::swap(small, large);
        filled = filled && F::set(j, pages, small, fs0, F::on(true));
        line("five fills, five steps", filled && j.size() == 5 && pages.strays() == 0);
        auto shown = Document::compose(pages.sources(), pages.records(), locked, err, pages.doc().fields);
        auto value = [](const Document& d, const std::string& name, double x = -1) {
            for (const auto& f : d.fields(0))
                if (f.name == name && (x < 0 || std::abs(f.box.x - x) < 1)) return f.value;
            return std::string("?");
        };
        line("the view shows the values",
             value(*shown, "name") == "Bob Smithers" && value(*shown, "agree") == "1" && value(*shown, "colour") == "#2" &&
             value(*shown, "fruit") == "#1" && value(*shown, "size", small.box.x) == "1" &&
             value(*shown, "size", large.box.x) == "0");
        auto* before = paint(*shown);
        line("filling changes what's drawn", blank && differ(before, blank) > 10);
        if (blank) cairo_surface_destroy(blank);

        auto save = [&](const std::string& name, const core::Lock& lock) {
            const std::string to = (dir / name).string();
            std::vector<std::string> temps;
            std::string e;
            auto srcs = Document::filled_sources(pages.sources(), pages.doc().fields, temps, e);
            const bool ok = !srcs.empty() && save::write_pdf(srcs, pages.records(), to, e, lock);
            for (const auto& t : temps) { std::error_code ec; fs::remove(t, ec); Document::remember_password(t, ""); }
            if (!ok) std::cout << "forms: save " << name << ": " << e << "\n";
            return ok ? to : std::string{};
        };
        auto check_saved = [&](const std::string& path, const std::string& pw, const std::string& what) {
            std::string e;
            auto back = opens(path, pw, e);
            const bool vals = back && value(*back, "name") == "Bob Smithers" && value(*back, "agree") == "1" &&
                              value(*back, "colour") == "#2" && value(*back, "fruit") == "#1" &&
                              value(*back, "size", small.box.x) == "1" && value(*back, "size", large.box.x) == "0";
            const int nf = back ? acro_fields(path, pw) : -1;
            int rows = -1;
            if (back) { auto* after = paint(*back); rows = differ(before, after); cairo_surface_destroy(after); }
            std::cout << "forms: " << what << ": values " << (vals ? "kept" : "LOST") << ", " << nf
                      << " fields in /AcroForm, " << rows << " rows differ from Lookit's view"
                      << (vals && nf == 5 && rows >= 0 && rows <= 4 ? "" : "  <- off") << "\n";
            if (!(vals && nf == 5 && rows >= 0 && rows <= 4)) ++bad;
        };

        using L = core::Lock;
        const auto kept = save("form-kept.pdf", {});
        line("saved keeping the lock: still asks for the password", !kept.empty() && !opens(kept, "", err) &&
                                                                    err == Document::kLocked);
        if (!kept.empty()) check_saved(kept, "s3cret", "kept lock, opened with its password");
        const auto open_copy = save("form-open.pdf", {L::Mode::None, ""});
        if (!open_copy.empty()) check_saved(open_copy, "", "password removed, opens without one");
        const auto relocked = save("form-new.pdf", {L::Mode::Password, "n3w pass"});
        line("a new password: the old one doesn't open it", !relocked.empty() && !opens(relocked, "s3cret", err) &&
                                                            err == Document::kLocked);
        if (!relocked.empty()) check_saved(relocked, "n3w pass", "new password");
        {   // the lock comes from the record, the form from the page: a duplicated page keeps a form too
            const bool dup = core::pages::duplicate(j, pages, pages.id_at(0));
            const auto two = save("form-two.pdf", {L::Mode::None, ""});
            std::string e;
            auto back = dup && !two.empty() ? opens(two, "", e) : nullptr;
            const bool ok = back && back->count() == 2 && back->fields(0).size() == 6 && back->fields(1).size() == 6 &&
                            value(*back, "name") == "Bob Smithers" && acro_fields(two, "") == 10;
            line("a duplicated form page brings its own fields (10 in /AcroForm)", ok);
            j.undo();
        }
        {   // a form never filled, saved: still a form, still its own values
            core::Pages plain;
            plain.load(form, 1);
            std::string e;
            const std::string to = (dir / "form-plain.pdf").string();
            const bool ok = save::write_pdf(plain.sources(), plain.records(), to, e) && acro_fields(to, "") == 5;
            auto back = ok ? opens(to, "", e) : nullptr;
            line("an unfilled form saves as a form with the file's values",
                 back && value(*back, "name") == "Ann" && value(*back, "size", large.box.x) == "1");
        }
        cairo_surface_destroy(before);
    }

    std::cout << (bad ? "FAIL" : "OK") << "\n";
    return bad ? 1 : 0;
}
