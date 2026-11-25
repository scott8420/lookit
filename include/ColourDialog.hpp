#pragma once
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/colordialog.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ColourDialog -- Menu -> Colour… (s014). The skin's base colour: the presets
// as swatches, Custom… (GTK's colour picker), and Reset to Teal.
//
// Not modal, so the window behind it is the preview: a click applies at once
// (App::set_tint re-tints the stylesheet and remembers the choice), and the
// dialog itself wears the skin, so it changes too. Each swatch is painted with
// what the colour actually does to the skin (its tinted gradient), not the
// raw base, so what you click is what you get.
//
// Hide-on-close singleton, like the others (docs/resources-and-dialog-
// lifetime.md). Not an undo step: it is a setting, not a change to the
// document, and Ctrl+Z belongs to the document. Reset to Teal is the way back.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class ColourDialog : public Gtk::Window {
public:
    using Pick    = std::function<void(const std::string& hex)>;
    using Current = std::function<std::string()>;
    ColourDialog(Gtk::Window& parent, Current current, Pick pick);

    void show_current();   // ring the current swatch, say its name

private:
    void choose_custom();
    void add_swatch(const std::string& hex, const std::string& tip, std::function<void()> on_click);

    Current     m_current;
    Pick        m_pick;
    Gtk::Box    m_box{Gtk::Orientation::VERTICAL, 12};
    Gtk::Label  m_title, m_detail, m_now;
    Gtk::Box    m_swatches{Gtk::Orientation::HORIZONTAL, 10};
    Gtk::Box    m_buttons{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_custom{"Custom…"}, m_reset{"Reset to Teal"}, m_close{"Close"};
    // Presets in order, then the custom one (last). Heap-held: each draw
    // function keeps a pointer to its own.
    struct Swatch { std::string hex; Gtk::Button* button; Gtk::DrawingArea* area; bool current; };
    std::vector<std::unique_ptr<Swatch>> m_swatch;
    Glib::RefPtr<Gtk::CssProvider> m_css;
    Glib::RefPtr<Gtk::ColorDialog> m_picker;
};

}  // namespace lookit
