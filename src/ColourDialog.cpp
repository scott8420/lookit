#include "ColourDialog.hpp"
#include "Log.hpp"
#include "core/Tint.hpp"

#include <gdkmm/display.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <cairomm/context.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/stylecontext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lookit {

namespace {

const std::string kCss =
    ".swatch { min-width: 0; min-height: 0; padding: 0; border: none; outline: none;"
    "          background: none; box-shadow: none; border-radius: 50%; }\n"
    ".colour-now { font-weight: 600; }\n";

// What a base colour does to the skin, as a swatch: the Home gradient's light,
// middle and deep stops, put through the same turn as the stylesheet. Painted
// with Cairo, not CSS: a CSS gradient in a round button came out as an octagon
// under GTK's Cairo renderer.
void paint_swatch(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h,
                  const std::string& hex, bool current, bool hover) {
    const double r = std::min(w, h) / 2.0, cx = w / 2.0, cy = h / 2.0;
    const core::Rgb base = *core::parse_hex(hex);
    auto stop = [&](double at, const char* lit) {
        return std::pair<double, core::Rgb>(at, core::tint_color(*core::parse_hex(lit), base));
    };
    auto g = Cairo::LinearGradient::create(cx - r, cy - r, cx + r, cy + r);
    for (auto [at, c] : {stop(0.0, "#99e9f2"), stop(0.45, "#15aabf"), stop(1.0, "#0b6274")})
        g->add_color_stop_rgb(at, c.r, c.g, c.b);
    cr->arc(cx, cy, r - 5, 0, 2 * M_PI);
    cr->set_source(g);
    cr->fill();
    if (current || hover) {
        cr->arc(cx, cy, r - 1.5, 0, 2 * M_PI);
        cr->set_line_width(current ? 3 : 2);
        cr->set_source_rgba(1, 1, 1, current ? 1.0 : 0.5);
        cr->stroke();
    }
}

}  // namespace

ColourDialog::ColourDialog(Gtk::Window& parent, Current current, Pick pick)
    : m_current(std::move(current)), m_pick(std::move(pick)) {
    set_transient_for(parent);
    set_modal(false);            // the window behind is the preview
    set_hide_on_close(true);
    set_resizable(false);
    set_title("Colour");
    add_css_class("lookit");     // wear the skin, so the dialog previews too
    add_css_class("colour-dialog");
    auto* hb = Gtk::make_managed<Gtk::HeaderBar>();   // as AboutWindow: the skin's clear header
    hb->set_show_title_buttons(true);
    set_titlebar(*hb);

    m_box.set_margin(20);
    m_box.set_margin_top(4);
    m_title.set_text("Base colour");
    m_title.add_css_class("title-3");
    m_title.set_xalign(0);
    m_detail.set_text("Lookit's window takes on this colour. Text stays as easy to read as in teal.");
    m_detail.set_wrap(true);
    m_detail.set_max_width_chars(44);
    m_detail.set_xalign(0);
    m_detail.add_css_class("info-key");
    m_now.set_xalign(0);
    m_now.add_css_class("colour-now");

    for (const auto& p : core::tint_presets()) {
        const std::string hex = p.hex;
        add_swatch(hex, p.label, [this, hex] { m_pick(hex); show_current(); });
    }
    // The custom swatch: shown only while a custom colour is in use; a click
    // reopens the picker on it.
    add_swatch(core::kDefaultTint, "Custom", [this] { choose_custom(); });

    m_css = Gtk::CssProvider::create();
    m_css->load_from_data(kCss);
    Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), m_css,
                                                GTK_STYLE_PROVIDER_PRIORITY_USER);

    m_buttons.set_margin_top(6);
    m_close.set_hexpand(true);
    m_close.set_halign(Gtk::Align::END);
    m_buttons.append(m_custom);
    m_buttons.append(m_reset);
    m_buttons.append(m_close);
    for (auto* w : std::initializer_list<Gtk::Widget*>{&m_title, &m_detail, &m_swatches, &m_now, &m_buttons})
        m_box.append(*w);
    set_child(m_box);

    m_picker = Gtk::ColorDialog::create();
    m_picker->set_title("Custom Colour");
    m_picker->set_with_alpha(false);

    m_custom.signal_clicked().connect(sigc::mem_fun(*this, &ColourDialog::choose_custom));
    m_reset.signal_clicked().connect([this] { m_pick(core::kDefaultTint); show_current(); });
    m_close.signal_clicked().connect([this] { set_visible(false); });
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) {
        if (kv == GDK_KEY_Escape) { set_visible(false); return true; }
        return false;
    }, false);
    add_controller(keys);
    show_current();
}

void ColourDialog::add_swatch(const std::string& hex, const std::string& tip,
                              std::function<void()> on_click) {
    auto* b = Gtk::make_managed<Gtk::Button>();
    b->add_css_class("swatch");
    b->set_tooltip_text(tip);
    b->set_valign(Gtk::Align::CENTER);
    auto* area = Gtk::make_managed<Gtk::DrawingArea>();
    area->set_content_width(44);
    area->set_content_height(44);
    b->set_child(*area);
    b->signal_clicked().connect(std::move(on_click));
    m_swatches.append(*b);
    m_swatch.push_back(std::make_unique<Swatch>(Swatch{hex, b, area, false}));
    Swatch* sw = m_swatch.back().get();
    area->set_draw_func([sw](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const bool hover = (sw->button->get_state_flags() & Gtk::StateFlags::PRELIGHT) == Gtk::StateFlags::PRELIGHT;
        paint_swatch(cr, w, h, sw->hex, sw->current, hover);
    });
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_enter().connect([area](double, double) { area->queue_draw(); });
    motion->signal_leave().connect([area] { area->queue_draw(); });
    b->add_controller(motion);
}

void ColourDialog::show_current() {
    const std::string now = m_current();
    const std::size_t presets = core::tint_presets().size();
    bool preset = false;
    for (std::size_t i = 0; i < presets; ++i) {
        m_swatch[i]->current = m_swatch[i]->hex == now;
        preset = preset || m_swatch[i]->current;
        m_swatch[i]->area->queue_draw();
    }
    Swatch& custom = *m_swatch.back();
    custom.button->set_visible(!preset);
    custom.current = !preset;
    if (!preset) custom.hex = now;
    custom.area->queue_draw();
    m_reset.set_sensitive(now != core::kDefaultTint);
    m_now.set_text("Now: " + core::tint_label(now) + "  " + now);
}

void ColourDialog::choose_custom() {
    Gdk::RGBA start(m_current());
    m_picker->choose_rgba(*this, start, [this](const Glib::RefPtr<Gio::AsyncResult>& r) {
        try {
            const Gdk::RGBA c = m_picker->choose_rgba_finish(r);
            char buf[8];
            std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                          static_cast<int>(std::lround(c.get_red() * 255)),
                          static_cast<int>(std::lround(c.get_green() * 255)),
                          static_cast<int>(std::lround(c.get_blue() * 255)));
            m_pick(buf);
            show_current();
        } catch (const Glib::Error&) {
            // Cancelled or dismissed: nothing changes.
        }
    });
}

}  // namespace lookit
