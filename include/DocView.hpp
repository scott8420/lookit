#pragma once
#include "Document.hpp"
#include "core/Layout.hpp"

#include <gdkmm/texture.h>
#include <gtkmm/scrolledwindow.h>
#include <sigc++/signal.h>

#include <map>
#include <cstdint>
#include <gtkmm/gesturedrag.h>
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
    // The same document, edited (page tools): keep the page, zoom and fit.
    void refresh(std::shared_ptr<Document> doc, int page);
    const std::shared_ptr<Document>& document() const { return m_doc; }

    void zoom_in();
    void zoom_out();
    void zoom_actual();          // 100%
    void fit_width();
    void fit_page();
    void set_book(bool on);
    // Find-in-document: every hit is tinted; `show_hit` makes one current
    // (stronger tint) and scrolls it to the middle of the view.
    void set_hits(std::vector<core::search::Hit> hits);
    void show_hit(int index);      // Book view: spreads, page 1 alone on the right
    bool book() const { return m_mode == core::layout::Mode::Book; }
    // Paged (default): only the current page -- or facing pair -- is shown,
    // fitted to the window until you zoom. Off: one continuous column.
    void set_paged(bool on);
    bool paged() const { return m_paged; }
    void go_to(int page);        // 0-based
    void next_page();
    void prev_page();

    double scale() const { return m_scale; }
    Fit    fit() const { return m_fit; }
    int    page() const { return m_page; }    // 0-based; -1 when empty
    int    count() const;

    // Page, scale or document changed -- the toolbar repaints from the getters.
    sigc::signal<void()>& signal_changed() { return m_changed; }

    // ── marks on the page: signatures (s008), markup (s009) ──
    // Select: click a mark to select it; drag to move, drag its corner (a
    // line: either end) to size it; double-click a note or text to edit it.
    // A drawing tool: drag on a page to make one (a note: click). Nothing
    // changes the document here: a release hands the Shell the finished mark,
    // which makes it one undo step.
    enum class Tool { Select, Highlight, Note, Box, Ellipse, Line, Arrow, Text };
    void set_tool(Tool t);
    Tool tool() const { return m_tool; }
    void set_pen(std::uint32_t color, double width) { m_color = color; m_width = width; }
    struct MarkSel { int page = -1, index = -1; bool valid() const { return page >= 0 && index >= 0; } };
    MarkSel selected_mark() const { return m_sel; }
    void select_mark(int page, int index);
    void clear_mark();
    // (page, index, the changed mark, verb: "Move" / "Resize")
    sigc::signal<void(int, int, core::Mark, std::string)>& signal_mark_edit() { return m_mark_edit; }
    // (page, a new mark) -- notes and text come without text yet
    sigc::signal<void(int, core::Mark)>& signal_mark_create() { return m_mark_create; }
    // (page, index) -- double-click on a note or a text box
    sigc::signal<void(int, int)>& signal_mark_open() { return m_mark_open; }
    // Esc set the tool back to Select (the bar repaints its toggles)
    sigc::signal<void()>& signal_tool_reset() { return m_tool_reset; }
    // Where a mark is on screen (widget pixels), for a popover to point at.
    core::layout::Rect mark_screen_box(int page, int index) const;
    core::layout::Rect mark_screen_box(int page, const core::Mark& m) const;

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
    std::vector<core::search::Hit> m_hits;
    int    m_hit = -1;
    bool   m_paged = true;
    double m_flip = 0;                   // wheel push past an edge, toward a page turn
    void   turn(int dir, bool to_end);   // paged: next / previous row, top or bottom
    bool   at_top() const;
    bool   at_bottom() const;
    int    m_page = -1;
    int    m_vw = 0, m_vh = 0;           // viewport (canvas allocation)
    bool   m_live = false;               // a pinch is under way: stretch, don't re-render
    double m_pinch_base = 1.0;
    double m_mx = -1, m_my = -1;         // pointer, for Ctrl+scroll anchoring
    sigc::signal<void()> m_changed;

    // Marks: the tool, the pen, the selection, a drag under way.
    Tool          m_tool = Tool::Select;
    std::uint32_t m_color = 0xe01b24ffu;
    double        m_width = 2;
    MarkSel       m_sel;
    enum class DragMode { None, Move, Resize, Start, End, Create };
    struct MarkDrag {
        DragMode mode = DragMode::None;
        int page = -1;
        core::Mark start, live;            // unturned frame
        core::layout::Rect start_box;      // the seen frame, page points
        core::marks::Point press;          // the seen frame, page points
        core::marks::Point a0, b0;         // a line's ends at the press (seen)
    } m_drag;
    gint64 m_last_press = 0;               // double-click on a note / text
    MarkSel m_last_hit;
    sigc::signal<void(int, int, core::Mark, std::string)> m_mark_edit;
    sigc::signal<void(int, core::Mark)> m_mark_create;
    sigc::signal<void(int, int)> m_mark_open;
    sigc::signal<void()> m_tool_reset;
    int  page_under(double cx, double cy) const;          // content px -> page, -1
    std::vector<core::marks::Shape> shapes(int page) const;   // content px
    std::vector<core::Mark> live_marks(int page) const;   // with a drag's live mark in place
    void mark_cursor(double x, double y);
    void snapshot_marks(GtkSnapshot* s, int page, const core::layout::Rect& r, double ox, double oy,
                        const Glib::RefPtr<Gdk::Texture>& paper, const graphene_rect_t& box);
    void drag_begin(double x, double y, Gtk::GestureDrag* g);
    void drag_update(double dx, double dy);
    void drag_end();
};

}  // namespace lookit
