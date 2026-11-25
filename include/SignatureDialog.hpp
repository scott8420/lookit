#pragma once
#include <gtkmm/button.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// SignatureDialog -- draw your signature once (or import a picture of one);
// Lookit keeps it as a PNG in its data folder and places it from then on.
//
// Hide-on-close singleton, like AboutWindow (docs/resources-and-dialog-
// lifetime.md): the Shell builds it once and presents it again.
//
// A drawing is strokes on a pad, rendered at 3x into a PNG cropped to the ink.
// An imported picture (a scan or a photo of a signature on paper) is made into
// ink on clear by core::sign::ink -- white paper drops out -- and cropped the
// same way. Each save is a new file (signature-<time>.png): a signature already
// placed keeps its own picture, so undo never finds it changed.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class SignatureDialog : public Gtk::Window {
public:
    explicit SignatureDialog(Gtk::Window& parent);
    ~SignatureDialog() override;

    // Show it, with the kept signature (if any) on the pad. `done` gets the
    // new PNG's path when the person presses Use Signature.
    void ask(const std::string& current, std::function<void(const std::string&)> done);

    // Where signatures live, and the newest one ("" when none).
    static std::string folder();
    static std::string latest();

private:
    struct Pt { double x, y; };
    void clear();
    void import_picture();
    void use();
    void draw_pad(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    bool load_rgba(const std::string& path, bool make_ink);
    void sync();

    Gtk::DrawingArea m_pad;
    Gtk::Label       m_hint;
    Gtk::Button      m_clear{"Clear"}, m_import{"Import Picture…"}, m_cancel{"Cancel"},
                     m_use{"Use Signature"};
    std::vector<std::vector<Pt>> m_strokes;
    // A picture on the pad instead of strokes: straight RGBA, cropped to ink.
    std::vector<std::uint8_t> m_pic;
    int m_pic_w = 0, m_pic_h = 0;
    std::function<void(const std::string&)> m_done;
};

}  // namespace lookit
