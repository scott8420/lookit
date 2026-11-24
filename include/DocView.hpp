#pragma once
#include "Document.hpp"
#include "core/Layout.hpp"

#include <gdkmm/texture.h>
#include <gtkmm/scrolledwindow.h>
#include <sigc++/signal.h>

#include <map>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// DocView -- the page view: a ScrolledWindow whose child is LookitCanvas, a
// small GtkScrollable widget (DocView.cpp, C) that is only ever viewport-sized.
//
// Why not a DrawingArea in the ScrolledWindow: GTK4 would make one Cairo node
// the size of the WHOLE document (hundreds of pages tall) on every redraw. The
// canvas instead takes the scroll adjustments itself, draws only what is on
// screen, and draws each page as a cached GPU texture -- so scrolling is a
// translate, and a pinch stretches textures until the fingers lift.
//
// The arithmetic (where pages sit, fit, zoom steps, the page you're on) is
// core::layout, held by the selftest. This file is the GTK half.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class DocView : public Gtk::ScrolledWindow {
public:
    enum class Fit { Width, Page, None };

    DocView();
    ~DocView() override;

    void set_document(std::shared_ptr<Document> doc);   // null clears
    const std::shared_ptr<Document>& document() const { return m_doc; }

    void zoom_in();
    void zoom_out();
    void zoom_actual();          // 100%
    void fit_width();
    void fit_page();
    void set_book(bool on);      // Book view: spreads, page 1 alone on the right
    bool book() const { return m_mode == core::layout::Mode::Book; }
    void go_to(int page);        // 0-based
    void next_page();
    void prev_page();

    double scale() const { return m_scale; }
    Fit    fit() const { return m_fit; }
    int    page() const { return m_page; }    // 0-based; -1 when empty
    int    count() const;

    // Page, scale or document changed -- the toolbar repaints from the getters.
    sigc::signal<void()>& signal_changed() { return m_changed; }

    // ── called by the C canvas ──
    void canvas_allocate(int w, int h);
    void canvas_snapshot(GtkSnapshot* s, int w, int h);
    void canvas_scrolled();

private:
    struct Cached { Glib::RefPtr<Gdk::Texture> tex; double scale = 0; };

    void relayout();                                  // layout + adjustments from m_scale
    void set_scale(double s, Fit fit, double ax, double ay);   // anchor in viewport px
    void configure_adjustments();
    Glib::RefPtr<Gdk::Texture> render_page(int i, double scale);
    void evict(int first, int last);
    void update_page();

    GtkWidget* m_canvas = nullptr;
    std::shared_ptr<Document> m_doc;
    std::vector<core::layout::Size> m_sizes;
    core::layout::Placed m_placed;
    std::map<int, Cached> m_cache;
    double m_scale = 1.0;
    Fit    m_fit = Fit::Width;
    core::layout::Mode m_mode = core::layout::Mode::Continuous;
    int    m_page = -1;
    int    m_vw = 0, m_vh = 0;           // viewport (canvas allocation)
    bool   m_live = false;               // a pinch is under way: stretch, don't re-render
    double m_pinch_base = 1.0;
    double m_mx = -1, m_my = -1;         // pointer, for Ctrl+scroll anchoring
    sigc::signal<void()> m_changed;
};

}  // namespace lookit
