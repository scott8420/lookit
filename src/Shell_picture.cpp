#include "Shell.hpp"
#include "Export.hpp"
#include "Log.hpp"
#include "PictureArt.hpp"
#include "core/FileKind.hpp"

#include <glibmm/main.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/popover.h>
#include <gtkmm/separator.h>

#include <filesystem>

// Shell_picture.cpp -- PICTURE (s010). A picture is a first-class kind: the
// picture bar browses its folder (Left / Right) and edits it -- rotate, flip,
// crop, resize, light. Every edit is one change to the picture's one edit
// record through the picture journal (core/Picture: the ONE door), so each is
// an undo step and the stray gate holds it. Nothing touches the file until
// Save, which writes what you see in the format its name asks for.

namespace lookit {

namespace fs = std::filesystem;
namespace PI = core::picture;
namespace PV = core::pic;

namespace {
// The formats a picture saves to, by the name's extension.
bool format_for(const std::string& path, exporter::Format& f) {
    std::string e = fs::path(path).extension().string();
    for (auto& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (e == ".png")                    { f = exporter::Format::Png;  return true; }
    if (e == ".jpg" || e == ".jpeg")    { f = exporter::Format::Jpeg; return true; }
    if (e == ".tif" || e == ".tiff")    { f = exporter::Format::Tiff; return true; }
    if (e == ".bmp")                    { f = exporter::Format::Bmp;  return true; }
    if (e == ".pdf")                    { f = exporter::Format::Pdf;  return true; }
    return false;
}

Gtk::Button* icon_button(const char* icon, const char* tip, const char* action) {
    auto* b = Gtk::make_managed<Gtk::Button>();
    b->set_icon_name(icon);
    b->set_tooltip_text(tip);
    b->add_css_class("flat");
    if (action) b->set_action_name(action);
    return b;
}
}  // namespace

void Shell::bind_picture() {  // picture: the bar, actions, view signals
    m_pic.set_door([this] { return m_pic_journal.open(); });
    m_pic_journal.set_on_change([this] {
        if (!m_pic_on) return;
        // Coalesce: a slider can move many times a frame; draw once.
        if (!m_pic_idle.connected())
            m_pic_idle = Glib::signal_idle().connect([this] { apply_picture(); return false; });
        sync_edit_ui();
    });

    m_act_flip_h = add_action("flip-horizontal", [this] { picture_flip(true); });
    m_act_flip_v = add_action("flip-vertical", [this] { picture_flip(false); });
    m_act_crop = add_action("crop", [this] { if (m_pic_on) on_crop(!m_doc_view.cropping()); });
    m_act_resize = add_action("resize", [this] { if (m_pic_on && m_pic_resize_btn) m_pic_resize_btn->popup(); });
    m_act_light = add_action("light", [this] { if (m_pic_on && m_pic_light_btn) m_pic_light_btn->popup(); });
    m_act_revert = add_action("revert-picture", [this] {
        if (m_pic_on && PV::reset(m_pic_journal, m_pic)) show_toast("Back to the original -- Ctrl+Z to undo");
    });
    add_action("picture-prev", [this] { picture_browse(-1); });
    add_action("picture-next", [this] { picture_browse(+1); });

    // ── the bar ──
    m_picture_bar.add_css_class("view-bar");
    m_picture_bar.set_halign(Gtk::Align::CENTER);
    m_pic_prev = icon_button("go-previous-symbolic", "Previous picture in the folder (←)", "win.picture-prev");
    m_pic_next = icon_button("go-next-symbolic", "Next picture in the folder (→)", "win.picture-next");
    m_pic_where = Gtk::make_managed<Gtk::Label>();
    m_pic_where->add_css_class("page-label");
    m_pic_where->set_width_chars(8);
    m_picture_bar.append(*m_pic_prev);
    m_picture_bar.append(*m_pic_where);
    m_picture_bar.append(*m_pic_next);
    m_picture_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    m_picture_bar.append(*icon_button("object-rotate-left-symbolic", "Rotate left (Ctrl+L)", "win.rotate-left"));
    m_picture_bar.append(*icon_button("object-rotate-right-symbolic", "Rotate right (Ctrl+R)", "win.rotate-right"));
    m_picture_bar.append(*icon_button("object-flip-horizontal-symbolic", "Flip left-right (Ctrl+Shift+H)",
                                      "win.flip-horizontal"));
    m_picture_bar.append(*icon_button("object-flip-vertical-symbolic", "Flip upside down (Ctrl+Shift+V)",
                                      "win.flip-vertical"));
    m_picture_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

    // Crop: a toggle; while on, Apply / Cancel show beside it.
    m_pic_crop = Gtk::make_managed<Gtk::ToggleButton>();
    m_pic_crop->set_icon_name("lookit-crop-symbolic");
    m_pic_crop->set_tooltip_text("Crop -- drag a frame, Enter to crop, Esc to cancel (Ctrl+Shift+X)");
    m_pic_crop->add_css_class("flat");
    m_pic_crop->signal_toggled().connect([this] {
        if (m_pic_syncing) return;
        on_crop(m_pic_crop->get_active());
    });
    m_picture_bar.append(*m_pic_crop);
    m_pic_crop_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto* crop_go = Gtk::make_managed<Gtk::Button>("Crop");
    crop_go->add_css_class("suggested-action");
    crop_go->signal_clicked().connect([this] { crop_done(true); });
    auto* crop_no = Gtk::make_managed<Gtk::Button>("Cancel");
    crop_no->signal_clicked().connect([this] { crop_done(false); });
    m_pic_crop_box->append(*crop_go);
    m_pic_crop_box->append(*crop_no);
    m_pic_crop_box->set_visible(false);
    m_picture_bar.append(*m_pic_crop_box);

    // Resize: a popover with width / height, kept in proportion.
    m_pic_resize_btn = Gtk::make_managed<Gtk::MenuButton>();
    m_pic_resize_btn->set_icon_name("lookit-resize-symbolic");
    m_pic_resize_btn->set_tooltip_text("Resize (Ctrl+Shift+R)");
    m_pic_resize_btn->add_css_class("flat");
    {
        auto* pop = Gtk::make_managed<Gtk::Popover>();
        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
        box->set_margin(8);
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_pic_w = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(1, 1, 32768, 1, 100), 1, 0);
        m_pic_h = Gtk::make_managed<Gtk::SpinButton>(Gtk::Adjustment::create(1, 1, 32768, 1, 100), 1, 0);
        m_pic_w->set_width_chars(6);
        m_pic_h->set_width_chars(6);
        row->append(*m_pic_w);
        row->append(*Gtk::make_managed<Gtk::Label>("×"));
        row->append(*m_pic_h);
        row->append(*Gtk::make_managed<Gtk::Label>("pixels"));
        m_pic_keep = Gtk::make_managed<Gtk::CheckButton>("Keep proportions");
        m_pic_keep->set_active(true);
        auto* presets = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        for (int pct : {25, 50, 75}) {
            auto* b = Gtk::make_managed<Gtk::Button>(std::to_string(pct) + "%");
            b->add_css_class("flat");
            b->signal_clicked().connect([this, pct] {
                const auto [w, h] = PI::out_size(m_pic.edit(), m_pic.w(), m_pic.h());
                m_pic_syncing = true;
                m_pic_w->set_value(std::max(1, int(std::lround(w * pct / 100.0))));
                m_pic_h->set_value(std::max(1, int(std::lround(h * pct / 100.0))));
                m_pic_syncing = false;
            });
            presets->append(*b);
        }
        auto* go = Gtk::make_managed<Gtk::Button>("Resize");
        go->add_css_class("suggested-action");
        go->signal_clicked().connect([this, pop] { on_resize_apply(); pop->popdown(); });
        auto keep = [this](Gtk::SpinButton* changed, Gtk::SpinButton* other, bool width) {
            if (m_pic_syncing || !m_pic_keep->get_active()) return;
            const auto [w, h] = PI::out_size(m_pic.edit(), m_pic.w(), m_pic.h());
            m_pic_syncing = true;
            other->set_value(width ? PI::keep_aspect(changed->get_value_as_int(), w, h)
                                   : PI::keep_aspect(changed->get_value_as_int(), h, w));
            m_pic_syncing = false;
        };
        m_pic_w->signal_value_changed().connect([this, keep] { keep(m_pic_w, m_pic_h, true); });
        m_pic_h->signal_value_changed().connect([this, keep] { keep(m_pic_h, m_pic_w, false); });
        for (auto* sb : {m_pic_w, m_pic_h}) {   // Enter resizes (SpinButton's own "activate" is GTK 4.14+)
            auto k = Gtk::EventControllerKey::create();
            k->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);   // before the entry's own Enter
            k->signal_key_pressed().connect([this, pop, sb](guint kv, guint, Gdk::ModifierType) {
                if (kv != GDK_KEY_Return && kv != GDK_KEY_KP_Enter) return false;
                sb->update();
                on_resize_apply();
                pop->popdown();
                return true;
            }, false);
            sb->add_controller(k);
        }
        box->append(*row);
        box->append(*m_pic_keep);
        box->append(*presets);
        box->append(*go);
        pop->set_child(*box);
        pop->signal_show().connect([this] {
            const auto [w, h] = PI::out_size(m_pic.edit(), m_pic.w(), m_pic.h());
            m_pic_syncing = true;
            m_pic_w->set_value(w);
            m_pic_h->set_value(h);
            m_pic_syncing = false;
        });
        m_pic_resize_btn->set_popover(*pop);
    }
    m_picture_bar.append(*m_pic_resize_btn);

    // Light: brightness, contrast, saturation. One visit to the popover is one
    // undo step however much the sliders move (merge key per visit).
    m_pic_light_btn = Gtk::make_managed<Gtk::MenuButton>();
    m_pic_light_btn->set_icon_name("lookit-light-symbolic");
    m_pic_light_btn->set_tooltip_text("Light -- brightness, contrast, saturation (Ctrl+Shift+L)");
    m_pic_light_btn->add_css_class("flat");
    {
        auto* pop = Gtk::make_managed<Gtk::Popover>();
        auto* grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        grid->set_margin(8);
        const char* names[3] = {"Brightness", "Contrast", "Saturation"};
        for (int i = 0; i < 3; ++i) {
            auto* l = Gtk::make_managed<Gtk::Label>(names[i]);
            l->set_xalign(0);
            l->add_css_class("dim-label");
            auto* sc = Gtk::make_managed<Gtk::Scale>(Gtk::Adjustment::create(0, -100, 100, 1, 10),
                                                     Gtk::Orientation::HORIZONTAL);
            sc->set_size_request(220, -1);
            sc->set_draw_value(true);
            sc->set_digits(0);
            sc->add_mark(0, Gtk::PositionType::BOTTOM, "");
            sc->signal_value_changed().connect(sigc::mem_fun(*this, &Shell::on_light_changed));
            m_pic_light[i] = sc;
            grid->append(*l);
            grid->append(*sc);
        }
        auto* reset = Gtk::make_managed<Gtk::Button>("Reset");
        reset->set_halign(Gtk::Align::END);
        reset->signal_clicked().connect([this] {
            ++m_light_session;
            PV::light(m_pic_journal, m_pic, 0, 0, 0, "light-" + std::to_string(m_light_session));
            sync_picture_bar();
        });
        grid->append(*reset);
        pop->set_child(*grid);
        pop->signal_show().connect([this] { ++m_light_session; sync_picture_bar(); });
        m_pic_light_btn->set_popover(*pop);
    }
    m_picture_bar.append(*m_pic_light_btn);
    m_picture_bar.append(*icon_button("document-revert-symbolic", "Revert to the original (one undo step)",
                                      "win.revert-picture"));
    m_picture_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    m_pic_size = Gtk::make_managed<Gtk::Label>();
    m_pic_size->add_css_class("page-label");
    m_picture_bar.append(*m_pic_size);
    auto* save = Gtk::make_managed<Gtk::Button>("Save");
    save->set_tooltip_text("Save (Ctrl+S) -- over the file, in its own format");
    save->set_action_name("win.save");
    save->add_css_class("flat");
    auto* save_as = Gtk::make_managed<Gtk::Button>("Save As…");
    save_as->set_tooltip_text("Save as (Ctrl+Shift+S) -- PNG, JPEG, TIFF, BMP or PDF, by the name you give");
    save_as->set_action_name("win.save-as");
    save_as->add_css_class("flat");
    m_picture_bar.append(*save);
    m_picture_bar.append(*save_as);

    m_picture_rev.set_child(m_picture_bar);
    m_picture_rev.set_reveal_child(false);
    m_view.insert_child_after(m_picture_rev, m_markup_rev);

    m_doc_view.signal_crop_done().connect(sigc::mem_fun(*this, &Shell::crop_done));
    m_doc_view.signal_crop_changed().connect([this] { sync_picture_bar(); });
    sync_picture_bar();
}

std::shared_ptr<Document> Shell::picture_doc() const {  // picture: what you see now
    return m_doc_view.document();
}

void Shell::begin_picture(const std::shared_ptr<Document>& doc) {  // picture: a picture opened
    end_picture();
    cairo_surface_t* s = doc ? doc->surface() : nullptr;
    if (!s) return;
    m_pic_base = cairo_surface_reference(s);
    m_pic_path = doc->path();
    m_pic.load(cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
    m_pic_journal.clear();
    m_pic_on = true;
    m_dirty = false;
    m_doc_path = m_pic_path;
    // The folder's pictures, by name (the bytes are read only when you get there).
    m_pic_folder.clear();
    std::error_code ec;
    for (const auto& ent : fs::directory_iterator(fs::path(m_pic_path).parent_path(), ec)) {
        std::error_code e2;
        if (!ent.is_regular_file(e2)) continue;
        const auto name = ent.path().filename().string();
        if (core::kind_from_name(name) == core::Kind::Image || name == fs::path(m_pic_path).filename().string())
            m_pic_folder.push_back(name);
    }
    m_pic_folder = PI::sorted(std::move(m_pic_folder));
    sync_picture_bar();
}

void Shell::end_picture() {  // picture: something else opened
    if (m_pic_idle.connected()) m_pic_idle.disconnect();
    if (m_doc_view.cropping()) m_doc_view.set_crop(false);
    if (m_pic_base) cairo_surface_destroy(m_pic_base);
    if (m_pic_shaped) cairo_surface_destroy(m_pic_shaped);
    m_pic_base = m_pic_shaped = nullptr;
    m_pic_shaped_for = {};
    const bool was = m_pic_on;
    m_pic_on = false;
    m_pic_journal.clear();
    m_pic_folder.clear();
    if (was) m_doc_path.clear();
    sync_picture_bar();
}

// The journal changed: make what you see from the picture as it came. The
// geometry (orient / crop / resize) is kept between slider moves, so the
// light alone is redone while you drag.
void Shell::apply_picture() {  // picture: journal changed -> the view (on idle)
    if (!m_pic_on || !m_pic_base) return;
    const auto& e = m_pic.edit();
    core::PicEdit geo = e;
    geo.brightness = geo.contrast = geo.saturation = 0;
    if (!m_pic_shaped || !(geo == m_pic_shaped_for)) {
        if (m_pic_shaped) cairo_surface_destroy(m_pic_shaped);
        m_pic_shaped = picture_art::shape(m_pic_base, geo);
        m_pic_shaped_for = geo;
    }
    if (cairo_surface_status(m_pic_shaped) != CAIRO_STATUS_SUCCESS) {
        show_toast("The picture is too large to hold in memory at that size.", true);
        return;
    }
    auto doc = Document::picture(picture_art::light(m_pic_shaped, e), m_pic_path);
    m_doc_view.refresh(doc, 0);
    m_thumbs.set_document(doc);
    m_dirty = !PI::unchanged(e);
    if (auto lg = log::get(log::Area::Shell)) {
        const auto [w, h] = PI::out_size(e, m_pic.w(), m_pic.h());
        lg->info("picture: turn {} flip {} crop {}x{}+{}+{} out {}x{} light {}/{}/{}", e.turn, e.flip, e.crop.w,
                 e.crop.h, e.crop.x, e.crop.y, w, h, e.brightness, e.contrast, e.saturation);
    }
    refresh_info();
    sync_view_bar();
    sync_edit_ui();
    sync_picture_bar();
}

void Shell::sync_picture_bar() {  // picture: where, size, greys, sliders
    if (!m_pic_where) return;
    m_picture_rev.set_reveal_child(m_pic_on);
    for (auto& a : {m_act_crop, m_act_resize, m_act_light, m_act_flip_h, m_act_flip_v})
        if (a) a->set_enabled(m_pic_on);
    if (m_act_revert) m_act_revert->set_enabled(m_pic_on && !PI::unchanged(m_pic.edit()));
    if (!m_pic_on) return;
    const int i = PI::index_of(m_pic_folder, fs::path(m_pic_path).filename().string());
    const int n = int(m_pic_folder.size());
    m_pic_where->set_text(i >= 0 ? std::to_string(i + 1) + " of " + std::to_string(n) : "");
    m_pic_prev->set_sensitive(i > 0);
    m_pic_next->set_sensitive(i >= 0 && i + 1 < n);
    const bool cropping = m_doc_view.cropping();
    m_pic_syncing = true;
    m_pic_crop->set_active(cropping);
    const auto& e = m_pic.edit();
    const int vals[3] = {e.brightness, e.contrast, e.saturation};
    for (int k = 0; k < 3; ++k)
        if (m_pic_light[k] && int(m_pic_light[k]->get_value()) != vals[k]) m_pic_light[k]->set_value(vals[k]);
    m_pic_syncing = false;
    m_pic_crop_box->set_visible(cropping);
    std::string size;
    if (cropping) {
        // What the crop will make, on whole pixels -- not the frame's fractions.
        const auto c = PI::cropped(m_pic.edit(), m_doc_view.crop_frame(), m_pic.w(), m_pic.h());
        const auto [w, h] = PI::out_size(c, m_pic.w(), m_pic.h());
        size = "crop " + std::to_string(w) + " × " + std::to_string(h);
    } else {
        const auto [w, h] = PI::out_size(e, m_pic.w(), m_pic.h());
        size = std::to_string(w) + " × " + std::to_string(h);
    }
    m_pic_size->set_text(size);
}

void Shell::picture_rotate(int q) {  // picture
    if (!m_pic_on) return;
    if (m_doc_view.cropping()) m_doc_view.set_crop(false);
    PV::rotate(m_pic_journal, m_pic, q);
}

void Shell::picture_flip(bool horizontal) {  // picture
    if (!m_pic_on) return;
    if (m_doc_view.cropping()) m_doc_view.set_crop(false);
    PV::flip(m_pic_journal, m_pic, horizontal);
}

void Shell::on_crop(bool on) {  // picture: the crop frame on / off
    if (!m_pic_on) return;
    go("view");
    m_doc_view.set_crop(on);
    sync_picture_bar();
}

void Shell::crop_done(bool apply) {  // picture: Enter / Esc / Apply / Cancel
    if (!m_pic_on || !m_doc_view.cropping()) return;
    const auto frame = m_doc_view.crop_frame();
    m_doc_view.set_crop(false);
    if (apply && PV::crop(m_pic_journal, m_pic, frame)) show_toast("Cropped -- Ctrl+Z to undo");
    sync_picture_bar();
}

void Shell::on_resize_apply() {  // picture: the resize popover's button
    if (!m_pic_on || m_pic_syncing) return;
    PV::resize(m_pic_journal, m_pic, m_pic_w->get_value_as_int(), m_pic_h->get_value_as_int());
}

void Shell::on_light_changed() {  // picture: a slider moved
    if (!m_pic_on || m_pic_syncing) return;
    PV::light(m_pic_journal, m_pic, int(m_pic_light[0]->get_value()), int(m_pic_light[1]->get_value()),
              int(m_pic_light[2]->get_value()), "light-" + std::to_string(m_light_session));
}

void Shell::picture_browse(int dir) {  // picture: Left / Right in the folder
    if (!m_pic_on) return;
    const auto name = PI::step(m_pic_folder, fs::path(m_pic_path).filename().string(), dir);
    if (name.empty()) {
        show_toast(dir > 0 ? "That's the last picture in the folder." : "That's the first picture in the folder.");
        return;
    }
    open_path((fs::path(m_pic_path).parent_path() / name).string());   // asks first if there are changes
}

// Write what you see. A picture format goes over the file through a temporary
// beside it (a failed write leaves the old file whole), then the saved file is
// opened again as the clean original. A PDF is a copy: you stay on the picture.
bool Shell::picture_save_to(const std::string& path) {  // picture: any Export format by its extension
    exporter::Format f{};
    if (!format_for(path, f)) {
        show_toast("Lookit saves pictures as .png, .jpg, .tif, .bmp or .pdf -- pick one in the name.", true);
        return false;
    }
    const auto doc = picture_doc();
    if (!doc) return false;
    std::string err;
    if (f == exporter::Format::Pdf) {
        if (!exporter::write_picture_pdf(*doc, path, err)) { show_toast("Couldn't save: " + err, true); return false; }
        note_recent(path);
        show_toast("Saved a PDF copy: " + fs::path(path).filename().string());
        return true;
    }
    const std::string tmp = (fs::path(path).parent_path() / ("." + fs::path(path).filename().string() + ".lookit-tmp")).string();
    if (!exporter::write_page(*doc, 0, 72, f, tmp, err)) {
        std::error_code ec;
        fs::remove(tmp, ec);
        show_toast("Couldn't save: " + err, true);
        return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        show_toast("Couldn't save: " + ec.message(), true);
        return false;
    }
    if (auto lg = log::get(log::Area::Io)) lg->info("picture saved: {}", path);
    m_dirty = false;
    open_path(path);
    show_toast("Saved " + fs::path(path).filename().string());
    return true;
}

}  // namespace lookit
