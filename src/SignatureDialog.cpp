#include "SignatureDialog.hpp"
#include "Log.hpp"
#include "core/Sign.hpp"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/miscutils.h>
#include <gtkmm/box.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/gesturedrag.h>
#include <gdkmm/texture.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lookit {

namespace fs = std::filesystem;

namespace {
constexpr int kPadW = 560, kPadH = 200;
constexpr double kInk[3] = {0.06, 0.10, 0.30};   // a dark blue-black pen
constexpr double kPen = 2.6;                      // pad pixels
constexpr int kOut = 3;                           // the PNG is 3x the pad

// Straight RGBA -> Cairo ARGB32 (premultiplied, native order).
cairo_surface_t* to_surface(const std::vector<std::uint8_t>& rgba, int w, int h) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(s);
    auto* d = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(d + std::size_t(y) * std::size_t(stride));
        for (int x = 0; x < w; ++x) {
            const auto* p = &rgba[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4];
            const std::uint32_t a = p[3];
            row[x] = (a << 24) | ((p[0] * a / 255) << 16) | ((p[1] * a / 255) << 8) | (p[2] * a / 255);
        }
    }
    cairo_surface_mark_dirty(s);
    return s;
}

// Cairo ARGB32 -> straight RGBA, tightly packed.
std::vector<std::uint8_t> from_surface(cairo_surface_t* s, int& w, int& h) {
    cairo_surface_flush(s);
    w = cairo_image_surface_get_width(s);
    h = cairo_image_surface_get_height(s);
    std::vector<std::uint8_t> out(std::size_t(w) * std::size_t(h) * 4);
    core::convert::argb32_to_rgba(cairo_image_surface_get_data(s), cairo_image_surface_get_stride(s), w, h,
                                  out.data(), w * 4, false);
    return out;
}

// The part of `rgba` with ink in it, plus a small margin; empty when none.
std::vector<std::uint8_t> crop_to_ink(const std::vector<std::uint8_t>& rgba, int w, int h, int& ow, int& oh) {
    auto b = core::sign::ink_bounds(rgba.data(), w, h, w * 4);
    if (b.w <= 0) { ow = oh = 0; return {}; }
    const int m = 6;
    const int x0 = std::max(0, int(b.x) - m), y0 = std::max(0, int(b.y) - m);
    const int x1 = std::min(w, int(b.x + b.w) + m), y1 = std::min(h, int(b.y + b.h) + m);
    ow = x1 - x0;
    oh = y1 - y0;
    std::vector<std::uint8_t> out(std::size_t(ow) * std::size_t(oh) * 4);
    for (int y = 0; y < oh; ++y)
        std::copy_n(&rgba[(std::size_t(y + y0) * std::size_t(w) + std::size_t(x0)) * 4], std::size_t(ow) * 4,
                    &out[std::size_t(y) * std::size_t(ow) * 4]);
    return out;
}

// The pen's strokes, smoothed: a curve through each point to the next midpoint.
template <class Strokes>
void draw_strokes(cairo_t* cr, const Strokes& strokes) {
    cairo_set_source_rgb(cr, kInk[0], kInk[1], kInk[2]);
    cairo_set_line_width(cr, kPen);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (const auto& st : strokes) {
        if (st.size() == 1) {   // a dot
            cairo_arc(cr, st[0].x, st[0].y, kPen / 2, 0, 2 * G_PI);
            cairo_fill(cr);
            continue;
        }
        cairo_move_to(cr, st[0].x, st[0].y);
        for (std::size_t i = 1; i + 1 < st.size(); ++i) {
            const double mx = (st[i].x + st[i + 1].x) / 2, my = (st[i].y + st[i + 1].y) / 2;
            double x0, y0;
            cairo_get_current_point(cr, &x0, &y0);
            cairo_curve_to(cr, x0 + 2.0 / 3 * (st[i].x - x0), y0 + 2.0 / 3 * (st[i].y - y0),
                           mx + 2.0 / 3 * (st[i].x - mx), my + 2.0 / 3 * (st[i].y - my), mx, my);
        }
        cairo_line_to(cr, st.back().x, st.back().y);
        cairo_stroke(cr);
    }
}
}  // namespace

SignatureDialog::SignatureDialog(Gtk::Window& parent) {
    set_name("signature.window");
    set_title("Signature");
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_resizable(false);
    add_css_class("signature-window");

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
    box->set_margin(18);
    auto* title = Gtk::make_managed<Gtk::Label>("Your signature");
    title->add_css_class("title-2");
    title->set_halign(Gtk::Align::START);
    m_hint.set_halign(Gtk::Align::START);
    m_hint.set_wrap(true);
    m_hint.set_max_width_chars(60);   // the pad sets the width, not the hint
    m_hint.add_css_class("dim-label");

    m_pad.set_name("signature.pad");
    m_pad.set_content_width(kPadW);
    m_pad.set_content_height(kPadH);
    m_pad.add_css_class("signature-pad");
    gtk_widget_set_cursor_from_name(GTK_WIDGET(m_pad.gobj()), "crosshair");
    m_pad.set_draw_func(sigc::mem_fun(*this, &SignatureDialog::draw_pad));
    auto pen = Gtk::GestureDrag::create();
    pen->signal_drag_begin().connect([this](double x, double y) {
        if (!m_pic.empty()) { m_pic.clear(); m_pic_w = m_pic_h = 0; }   // drawing replaces a picture
        m_strokes.push_back({{x, y}});
        sync();
    });
    pen->signal_drag_update().connect([this, p = pen.get()](double dx, double dy) {
        double sx = 0, sy = 0;
        if (m_strokes.empty() || !p->get_start_point(sx, sy)) return;
        const Pt n{std::clamp(sx + dx, 0.0, double(m_pad.get_width())),
                   std::clamp(sy + dy, 0.0, double(m_pad.get_height()))};
        const Pt& last = m_strokes.back().back();
        if (std::hypot(n.x - last.x, n.y - last.y) < 0.8) return;
        m_strokes.back().push_back(n);
        m_pad.queue_draw();
    });
    m_pad.add_controller(pen);

    auto* buttons = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_use.add_css_class("suggested-action");
    buttons->append(m_clear);
    buttons->append(m_import);
    buttons->append(*spacer);
    buttons->append(m_cancel);
    buttons->append(m_use);
    m_clear.signal_clicked().connect(sigc::mem_fun(*this, &SignatureDialog::clear));
    m_import.signal_clicked().connect(sigc::mem_fun(*this, &SignatureDialog::import_picture));
    m_cancel.signal_clicked().connect([this] { hide(); });
    m_use.signal_clicked().connect(sigc::mem_fun(*this, &SignatureDialog::use));
    set_default_widget(m_use);

    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) {
        if (kv == GDK_KEY_Escape) { hide(); return true; }
        return false;
    }, false);
    add_controller(keys);

    box->append(*title);
    box->append(m_hint);
    box->append(m_pad);
    box->append(*buttons);
    set_child(*box);
    sync();
}

SignatureDialog::~SignatureDialog() = default;

std::string SignatureDialog::folder() {
    return (fs::path(Glib::get_user_data_dir()) / "lookit" / "signatures").string();
}

std::string SignatureDialog::latest() {
    std::error_code ec;
    std::string best;
    for (const auto& e : fs::directory_iterator(folder(), ec)) {
        const auto n = e.path().filename().string();
        if (n.rfind("signature-", 0) == 0 && e.path().extension() == ".png" && e.path().string() > best)
            best = e.path().string();
    }
    return best;
}

void SignatureDialog::ask(const std::string& current, std::function<void(const std::string&)> done) {
    m_done = std::move(done);
    m_strokes.clear();
    m_pic.clear();
    m_pic_w = m_pic_h = 0;
    if (!current.empty()) load_rgba(current, false);
    sync();
    present();
}

void SignatureDialog::sync() {
    const bool any = !m_strokes.empty() || !m_pic.empty();
    m_hint.set_text(!m_pic.empty() && m_strokes.empty()
                        ? "This is the signature Lookit keeps. Draw on the pad to replace it, or import a picture."
                        : "Sign on the pad with a mouse, touchpad or pen -- or import a picture of your "
                          "signature (white paper drops out).");
    m_clear.set_sensitive(any);
    m_use.set_sensitive(any);
    m_pad.queue_draw();
}

void SignatureDialog::clear() {
    m_strokes.clear();
    m_pic.clear();
    m_pic_w = m_pic_h = 0;
    sync();
}

void SignatureDialog::draw_pad(const Cairo::RefPtr<Cairo::Context>& ccr, int w, int h) {
    cairo_t* cr = ccr->cobj();
    // Paper, a rule to sign on, and an x.
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 0.55, 0.6, 0.65, 0.8);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 28, h * 0.74 + 0.5);
    cairo_line_to(cr, w - 28, h * 0.74 + 0.5);
    cairo_stroke(cr);
    cairo_move_to(cr, 30, h * 0.74 - 16);
    cairo_line_to(cr, 40, h * 0.74 - 6);
    cairo_move_to(cr, 40, h * 0.74 - 16);
    cairo_line_to(cr, 30, h * 0.74 - 6);
    cairo_stroke(cr);

    if (!m_pic.empty()) {
        cairo_surface_t* s = to_surface(m_pic, m_pic_w, m_pic_h);
        const double k = std::min({(w - 40.0) / m_pic_w, (h - 30.0) / m_pic_h, 1.0 / kOut * 1.6});
        cairo_save(cr);
        cairo_translate(cr, (w - m_pic_w * k) / 2, (h - m_pic_h * k) / 2);
        cairo_scale(cr, k, k);
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
        cairo_surface_destroy(s);
    }
    draw_strokes(cr, m_strokes);
}

bool SignatureDialog::load_rgba(const std::string& path, bool make_ink) {
    Glib::RefPtr<Gdk::Texture> tex;
    try { tex = Gdk::Texture::create_from_file(Gio::File::create_for_path(path)); }
    catch (const Glib::Error& e) {
        m_hint.set_text("Couldn't read that picture: " + std::string(e.what()));
        return false;
    }
    const int w = tex->get_width(), h = tex->get_height();
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(s);
    tex->download(cairo_image_surface_get_data(s), std::size_t(cairo_image_surface_get_stride(s)));
    cairo_surface_mark_dirty(s);
    int fw = 0, fh = 0;
    auto rgba = from_surface(s, fw, fh);
    cairo_surface_destroy(s);
    if (make_ink) core::sign::ink(rgba.data(), fw, fh, fw * 4);
    int cw = 0, ch = 0;
    auto cut = crop_to_ink(rgba, fw, fh, cw, ch);
    if (cut.empty()) {
        m_hint.set_text("No ink found in that picture -- is it a signature on light paper?");
        return false;
    }
    m_pic = std::move(cut);
    m_pic_w = cw;
    m_pic_h = ch;
    return true;
}

void SignatureDialog::import_picture() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("A picture of your signature");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto pics = Gtk::FileFilter::create();
    pics->set_name("Pictures");
    pics->add_pixbuf_formats();
    filters->append(pics);
    dialog->set_filters(filters);
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        Glib::RefPtr<Gio::File> f;
        try { f = dialog->open_finish(res); } catch (const Glib::Error&) { return; }
        m_strokes.clear();
        if (load_rgba(f->get_path(), true)) sync();
        else m_pad.queue_draw();
    });
}

void SignatureDialog::use() {
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    if (!m_strokes.empty()) {
        // The strokes at 3x, on clear, then cropped to the ink. (Drawing
        // replaces a picture, so there's never both.)
        const int pw = std::max(kPadW, m_pad.get_width()), ph = std::max(kPadH, m_pad.get_height());
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw * kOut, ph * kOut);
        cairo_t* cr = cairo_create(s);
        cairo_scale(cr, kOut, kOut);
        draw_strokes(cr, m_strokes);
        cairo_destroy(cr);
        int fw = 0, fh = 0;
        auto full = from_surface(s, fw, fh);
        cairo_surface_destroy(s);
        rgba = crop_to_ink(full, fw, fh, w, h);
    } else {
        rgba = m_pic;
        w = m_pic_w;
        h = m_pic_h;
    }
    if (rgba.empty()) return;

    std::error_code ec;
    fs::create_directories(folder(), ec);
    const std::string path =
        (fs::path(folder()) / ("signature-" + std::to_string(g_get_real_time()) + ".png")).string();
    cairo_surface_t* out = to_surface(rgba, w, h);
    const auto st = cairo_surface_write_to_png(out, path.c_str());
    cairo_surface_destroy(out);
    if (st != CAIRO_STATUS_SUCCESS) {
        m_hint.set_text("Couldn't keep the signature: " + std::string(cairo_status_to_string(st)));
        return;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("signature kept: {} ({}x{})", path, w, h);
    hide();
    if (m_done) m_done(path);
}

}  // namespace lookit
