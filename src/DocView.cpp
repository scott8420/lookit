#include "DocView.hpp"
#include "Log.hpp"
#include "MarkArt.hpp"
#include "core/Sign.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
#include <gtkmm/gesturedrag.h>
#include <giomm/file.h>
#include <gtkmm/gesturezoom.h>

#include <algorithm>
#include <cmath>

// ═════════════════════════════════════════════════════════════════════════════
// LookitCanvas -- the GObject half (C). A GtkWidget implementing GtkScrollable:
// it owns no state of its own beyond the two adjustments and forwards
// allocation, snapshot and scrolling to the DocView stored in "lookit-view".
// ═════════════════════════════════════════════════════════════════════════════
extern "C" {

typedef struct {
    GtkWidget      parent_instance;
    GtkAdjustment* hadj;
    GtkAdjustment* vadj;
    GtkScrollablePolicy hpolicy, vpolicy;
} LookitCanvas;
typedef struct { GtkWidgetClass parent_class; } LookitCanvasClass;

enum { PROP_0, PROP_HADJ, PROP_VADJ, PROP_HPOLICY, PROP_VPOLICY };

static GType lookit_canvas_get_type(void);
G_DEFINE_TYPE_WITH_CODE(LookitCanvas, lookit_canvas, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, nullptr))

static lookit::DocView* view_of(gpointer w) {
    return static_cast<lookit::DocView*>(g_object_get_data(G_OBJECT(w), "lookit-view"));
}

static void on_adj_value(GtkAdjustment*, gpointer w) {
    if (auto* v = view_of(w)) v->canvas_scrolled();
    gtk_widget_queue_draw(GTK_WIDGET(w));
}

static void set_adj(LookitCanvas* c, GtkAdjustment** slot, GtkAdjustment* adj) {
    if (*slot == adj) return;
    if (*slot) {
        g_signal_handlers_disconnect_by_func(*slot, (gpointer)on_adj_value, c);
        g_object_unref(*slot);
    }
    if (!adj) adj = gtk_adjustment_new(0, 0, 0, 0, 0, 0);
    *slot = GTK_ADJUSTMENT(g_object_ref_sink(adj));
    g_signal_connect(adj, "value-changed", G_CALLBACK(on_adj_value), c);
    gtk_widget_queue_allocate(GTK_WIDGET(c));
}

static void lookit_canvas_set_property(GObject* o, guint id, const GValue* v, GParamSpec* ps) {
    auto* c = reinterpret_cast<LookitCanvas*>(o);
    switch (id) {
        case PROP_HADJ:    set_adj(c, &c->hadj, GTK_ADJUSTMENT(g_value_get_object(v))); break;
        case PROP_VADJ:    set_adj(c, &c->vadj, GTK_ADJUSTMENT(g_value_get_object(v))); break;
        case PROP_HPOLICY: c->hpolicy = GtkScrollablePolicy(g_value_get_enum(v)); break;
        case PROP_VPOLICY: c->vpolicy = GtkScrollablePolicy(g_value_get_enum(v)); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void lookit_canvas_get_property(GObject* o, guint id, GValue* v, GParamSpec* ps) {
    auto* c = reinterpret_cast<LookitCanvas*>(o);
    switch (id) {
        case PROP_HADJ:    g_value_set_object(v, c->hadj); break;
        case PROP_VADJ:    g_value_set_object(v, c->vadj); break;
        case PROP_HPOLICY: g_value_set_enum(v, c->hpolicy); break;
        case PROP_VPOLICY: g_value_set_enum(v, c->vpolicy); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(o, id, ps);
    }
}

static void lookit_canvas_dispose(GObject* o) {
    auto* c = reinterpret_cast<LookitCanvas*>(o);
    for (GtkAdjustment** a : {&c->hadj, &c->vadj})
        if (*a) {
            g_signal_handlers_disconnect_by_func(*a, (gpointer)on_adj_value, c);
            g_clear_object(a);
        }
    G_OBJECT_CLASS(lookit_canvas_parent_class)->dispose(o);
}

static void lookit_canvas_size_allocate(GtkWidget* w, int width, int height, int) {
    if (auto* v = view_of(w)) v->canvas_allocate(width, height);
}

static void lookit_canvas_snapshot(GtkWidget* w, GtkSnapshot* s) {
    if (auto* v = view_of(w))
        v->canvas_snapshot(s, gtk_widget_get_width(w), gtk_widget_get_height(w));
}

static void lookit_canvas_class_init(LookitCanvasClass* k) {
    auto* oc = G_OBJECT_CLASS(k);
    oc->set_property = lookit_canvas_set_property;
    oc->get_property = lookit_canvas_get_property;
    oc->dispose = lookit_canvas_dispose;
    auto* wc = GTK_WIDGET_CLASS(k);
    wc->size_allocate = lookit_canvas_size_allocate;
    wc->snapshot = lookit_canvas_snapshot;
    g_object_class_override_property(oc, PROP_HADJ, "hadjustment");
    g_object_class_override_property(oc, PROP_VADJ, "vadjustment");
    g_object_class_override_property(oc, PROP_HPOLICY, "hscroll-policy");
    g_object_class_override_property(oc, PROP_VPOLICY, "vscroll-policy");
    gtk_widget_class_set_css_name(wc, "lookit-canvas");
}

static void lookit_canvas_init(LookitCanvas* c) {
    c->hpolicy = c->vpolicy = GTK_SCROLL_MINIMUM;
    set_adj(c, &c->hadj, nullptr);
    set_adj(c, &c->vadj, nullptr);
    gtk_widget_set_focusable(GTK_WIDGET(c), TRUE);
    gtk_widget_set_overflow(GTK_WIDGET(c), GTK_OVERFLOW_HIDDEN);
}

}  // extern "C"

// ═════════════════════════════════════════════════════════════════════════════
// DocView -- the C++ half.
// ═════════════════════════════════════════════════════════════════════════════
namespace lookit {

namespace L = core::layout;
using Rect_ = core::layout::Rect;

namespace {
LookitCanvas* canvas(GtkWidget* w) { return reinterpret_cast<LookitCanvas*>(w); }
// A page texture is capped near 16 Mpx; past that (very high zoom) the texture
// is stretched. Tiling at extreme zoom is on the backlog.
constexpr double kMaxPixels = 16.0e6;
constexpr std::size_t kMaxCached = 24;
}  // namespace

DocView::DocView() {
    set_name("view.doc");
    add_css_class("doc-view");
    set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    set_hexpand(true);
    set_vexpand(true);

    m_canvas = GTK_WIDGET(g_object_new(lookit_canvas_get_type(), nullptr));
    g_object_set_data(G_OBJECT(m_canvas), "lookit-view", this);
    gtk_widget_set_name(m_canvas, "view.canvas");
    gtk_scrolled_window_set_child(gobj(), m_canvas);
    Gtk::Widget* cw = Glib::wrap(m_canvas);

    // Ctrl+wheel zooms about the pointer. CAPTURE so the ScrolledWindow
    // doesn't scroll first; a plain wheel passes through untouched.
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) { m_mx = x; m_my = y; mark_cursor(x, y); });
    motion->signal_leave().connect([this] { m_mx = m_my = -1; });
    cw->add_controller(motion);

    auto wheel = Gtk::EventControllerScroll::create();
    wheel->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    wheel->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    wheel->signal_scroll().connect([this, w = wheel.get()](double, double dy) {
        if ((w->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType{})
            return false;
        if (!m_doc || dy == 0) return true;
        const double ax = m_mx >= 0 ? m_mx : m_vw / 2.0, ay = m_my >= 0 ? m_my : m_vh / 2.0;
        // Smooth (touchpad) deltas are small and many: zoom continuously.
        // A wheel click is one step on the ladder.
        if (std::abs(dy) < 1.0) set_scale(m_scale * std::exp(-dy * 0.05), Fit::None, ax, ay);
        else set_scale(dy < 0 ? L::zoom_in(m_scale) : L::zoom_out(m_scale), Fit::None, ax, ay);
        return true;
    }, false);
    add_controller(wheel);

    // Pinch: stretch the textures we have while the fingers move (m_live), and
    // re-render sharp when they lift.
    auto pinch = Gtk::GestureZoom::create();
    pinch->signal_begin().connect([this, p = pinch.get()](Gdk::EventSequence*) {
        m_pinch_base = m_scale;
        m_live = true;
    });
    pinch->signal_scale_changed().connect([this, p = pinch.get()](double d) {
        double cx = m_vw / 2.0, cy = m_vh / 2.0;
        p->get_bounding_box_center(cx, cy);
        set_scale(m_pinch_base * d, Fit::None, cx, cy);
    });
    pinch->signal_end().connect([this](Gdk::EventSequence*) {
        m_live = false;
        gtk_widget_queue_draw(m_canvas);
    });
    cw->add_controller(pinch);

    // Keys a reader expects. Page Up/Down and arrows scroll; Space pages
    // (Shift+Space back); Home/End; Left/Right step a page.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
        if (!m_doc) return false;
        // Escape: a drag goes back where it started; else a selected mark is
        // let go; else a drawing tool goes back to Select. Otherwise it's not
        // ours (the window closes drawers).
        if (m_crop_on && (kv == GDK_KEY_Escape || kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter)) {
            if (kv == GDK_KEY_Escape && m_cdrag.active) {   // a drag goes back first
                m_cdrag.active = false;
                m_crop = m_cdrag.before;
                gtk_widget_queue_draw(m_canvas);
                return true;
            }
            m_crop_done.emit(kv != GDK_KEY_Escape);
            return true;
        }
        if (kv == GDK_KEY_Escape) {
            if (m_drag.mode != DragMode::None) { m_drag.mode = DragMode::None; gtk_widget_queue_draw(m_canvas); return true; }
            if (m_sel.valid()) { clear_mark(); return true; }
            if (m_tool != Tool::Select) { set_tool(Tool::Select); m_tool_reset.emit(); return true; }
            return false;
        }
        const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
        if ((st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK)) != Gdk::ModifierType{})
            return false;
        auto* v = canvas(m_canvas)->vadj;
        const double page = gtk_adjustment_get_page_size(v);
        auto by = [&](double d) {
            // Paged: past the edge of the page turns it (Preview's Space).
            if (m_paged && d > 0 && at_bottom()) { turn(+1, false); return true; }
            if (m_paged && d < 0 && at_top())    { turn(-1, true);  return true; }
            gtk_adjustment_set_value(v, gtk_adjustment_get_value(v) + d);
            return true;
        };
        switch (kv) {
            case GDK_KEY_space:     return by(shift ? -page * 0.9 : page * 0.9);
            case GDK_KEY_Page_Down: return by(page * 0.9);
            case GDK_KEY_Page_Up:   return by(-page * 0.9);
            case GDK_KEY_Down:      return by(48);
            case GDK_KEY_Up:        return by(-48);
            case GDK_KEY_Home:      go_to(0); return true;
            case GDK_KEY_End:       go_to(count() - 1); return true;
            case GDK_KEY_Right:     if (count() <= 1) return false;   // a picture: the window browses the folder
                                    if (gtk_adjustment_get_upper(canvas(m_canvas)->hadj) >
                                        gtk_adjustment_get_page_size(canvas(m_canvas)->hadj) + 1) return false;
                                    next_page(); return true;
            case GDK_KEY_Left:      if (count() <= 1) return false;   // a picture: the window browses the folder
                                    if (gtk_adjustment_get_upper(canvas(m_canvas)->hadj) >
                                        gtk_adjustment_get_page_size(canvas(m_canvas)->hadj) + 1) return false;
                                    prev_page(); return true;
            default:                return false;
        }
    }, false);
    cw->add_controller(keys);

    // Paged: a wheel or two-finger push PAST the top or bottom turns the
    // page. A little resistance (m_flip) so arriving at the edge doesn't flip
    // at once; within the page the ScrolledWindow scrolls as usual.
    auto edge = Gtk::EventControllerScroll::create();
    edge->set_flags(Gtk::EventControllerScroll::Flags::VERTICAL);
    edge->signal_scroll().connect([this, e = edge.get()](double, double dy) {
        if (!m_paged || !m_doc || dy == 0) return false;
        if ((e->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{})
            return false;
        const bool down = dy > 0;
        if ((down && !at_bottom()) || (!down && !at_top())) { m_flip = 0; return false; }
        m_flip += std::abs(dy) < 1.0 ? std::abs(dy) * 0.15 : 1.0;   // smooth deltas are small
        if (m_flip < 1.0) return true;
        m_flip = 0;
        turn(down ? +1 : -1, !down);
        return true;
    }, false);
    cw->add_controller(edge);

    // Marks: a press in Select picks one up (or lets go); with a drawing tool
    // it starts a new one. The document is only changed at release.
    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect([this, g = drag.get()](double x, double y) { drag_begin(x, y, g); });
    drag->signal_drag_update().connect([this](double dx, double dy) { drag_update(dx, dy); });
    drag->signal_drag_end().connect([this](double, double) { drag_end(); });
    drag->signal_cancel().connect([this](Gdk::EventSequence*) {
        m_drag.mode = DragMode::None;
        gtk_widget_queue_draw(m_canvas);
    });
    cw->add_controller(drag);
}

DocView::~DocView() {
    if (m_canvas) g_object_set_data(G_OBJECT(m_canvas), "lookit-view", nullptr);
}

namespace M = core::marks;

void DocView::set_tool(Tool t) {
    m_tool = t;
    m_drag.mode = DragMode::None;
    if (t != Tool::Select) m_sel = {};
    gtk_widget_set_cursor_from_name(m_canvas, t == Tool::Select ? nullptr : t == Tool::Text ? "text" : "crosshair");
    gtk_widget_queue_draw(m_canvas);
}

void DocView::select_mark(int page, int index) {
    m_sel = {page, index};
    m_drag.mode = DragMode::None;
    gtk_widget_queue_draw(m_canvas);
    gtk_widget_grab_focus(m_canvas);
}

void DocView::clear_mark() {
    m_sel = {};
    m_drag.mode = DragMode::None;
    gtk_widget_queue_draw(m_canvas);
}

int DocView::page_under(double cx, double cy) const {
    for (int i = 0; i < int(m_placed.pages.size()); ++i) {
        const auto& r = m_placed.pages[std::size_t(i)];
        if (r.w > 0 && r.h > 0 && cx >= r.x && cx <= r.x + r.w && cy >= r.y && cy <= r.y + r.h) return i;
    }
    return -1;
}

std::vector<core::Mark> DocView::live_marks(int page) const {
    auto list = m_doc->marks(page);
    if (m_drag.mode != DragMode::None && m_drag.page == page) {
        if (m_drag.mode == DragMode::Create) list.push_back(m_drag.live);
        else if (m_sel.page == page && m_sel.index >= 0 && m_sel.index < int(list.size()))
            list[std::size_t(m_sel.index)] = m_drag.live;
    }
    return list;
}

std::vector<M::Shape> DocView::shapes(int page) const {
    std::vector<M::Shape> out;
    const auto& r = m_placed.pages[std::size_t(page)];
    const auto b = m_doc->base(page);
    const int t = m_doc->turn(page);
    auto px = [&](M::Point p) { return M::Point{r.x + p.x * m_scale, r.y + p.y * m_scale}; };
    for (const auto& m : m_doc->marks(page)) {
        M::Shape sh;
        const auto v = M::view_box(m, b.w, b.h, t);
        sh.box = {r.x + v.x * m_scale, r.y + v.y * m_scale, v.w * m_scale, v.h * m_scale};
        sh.resizable = M::resizable(m.kind);
        if (M::is_line(m.kind)) {
            const auto [a, z] = M::view_line(m, b.w, b.h, t);
            sh.line = true;
            sh.a = px(a);
            sh.b = px(z);
        }
        out.push_back(sh);
    }
    return out;
}

core::layout::Rect DocView::mark_screen_box(int page, int index) const {
    if (!m_doc || page < 0 || page >= int(m_placed.pages.size())) return {};
    const auto sh = shapes(page);
    if (index < 0 || index >= int(sh.size())) return {};
    auto* c = canvas(m_canvas);
    auto r = sh[std::size_t(index)].box;
    r.x -= gtk_adjustment_get_value(c->hadj);
    r.y -= gtk_adjustment_get_value(c->vadj);
    return r;
}

core::layout::Rect DocView::mark_screen_box(int page, const core::Mark& m) const {
    if (!m_doc || page < 0 || page >= int(m_placed.pages.size())) return {};
    const auto& r = m_placed.pages[std::size_t(page)];
    const auto b = m_doc->base(page);
    const auto v = M::view_box(m, b.w, b.h, m_doc->turn(page));
    auto* c = canvas(m_canvas);
    return {r.x + v.x * m_scale - gtk_adjustment_get_value(c->hadj), r.y + v.y * m_scale - gtk_adjustment_get_value(c->vadj),
            v.w * m_scale, v.h * m_scale};
}

void DocView::mark_cursor(double x, double y) {
    if (m_crop_on && m_doc && !m_cdrag.active && !m_placed.pages.empty()) {
        auto* c = canvas(m_canvas);
        const auto& r = m_placed.pages[0];
        const double px = (x + gtk_adjustment_get_value(c->hadj) - r.x) / m_scale;
        const double py = (y + gtk_adjustment_get_value(c->vadj) - r.y) / m_scale;
        using G = core::picture::Grip;
        const auto g = core::picture::crop_hit(m_crop, px, py, 10 / m_scale);
        gtk_widget_set_cursor_from_name(m_canvas, g == G::TopLeft || g == G::BottomRight ? "nwse-resize"
                                                  : g == G::TopRight || g == G::BottomLeft ? "nesw-resize"
                                                  : g == G::Body ? "move" : "crosshair");
        return;
    }
    if (!m_doc || m_drag.mode != DragMode::None || m_tool != Tool::Select) return;
    auto* c = canvas(m_canvas);
    const double cx = x + gtk_adjustment_get_value(c->hadj), cy = y + gtk_adjustment_get_value(c->vadj);
    const char* want = nullptr;
    if (const int pg = page_under(cx, cy); pg >= 0) {
        const auto h = M::hit(shapes(pg), cx, cy, 9);
        if (h.part == M::Part::Corner) want = "nwse-resize";
        else if (h.part == M::Part::Start || h.part == M::Part::End) want = "crosshair";
        else if (h.part == M::Part::Body) want = "move";
        if (!want && m_doc->kind() == core::Kind::Pdf) {   // a form field: text cursor, or a hand
            const auto& r = m_placed.pages[std::size_t(pg)];
            const auto b = m_doc->base(pg);
            const auto u = M::unturn_point({(cx - r.x) / m_scale, (cy - r.y) / m_scale}, b.w, b.h, m_doc->turn(pg));
            const auto& fl = page_fields(pg);
            if (const int f = core::forms::hit(fl, u.x, u.y); f >= 0 && core::forms::fillable(fl[std::size_t(f)]))
                want = fl[std::size_t(f)].kind == core::FieldKind::Text ? "text" : "pointer";
        }
    }
    gtk_widget_set_cursor_from_name(m_canvas, want);
}

void DocView::drag_begin(double x, double y, Gtk::GestureDrag* g) {
    m_drag.mode = DragMode::None;
    if (!m_doc) { g->set_state(Gtk::EventSequenceState::DENIED); return; }
    auto* c = canvas(m_canvas);
    const double cx = x + gtk_adjustment_get_value(c->hadj), cy = y + gtk_adjustment_get_value(c->vadj);
    const int pg = page_under(cx, cy);
    if (m_crop_on) {   // the crop frame: a corner, the body, or a new frame
        if (pg != 0) { g->set_state(Gtk::EventSequenceState::DENIED); return; }
        g->set_state(Gtk::EventSequenceState::CLAIMED);
        gtk_widget_grab_focus(m_canvas);
        const auto& r = m_placed.pages[0];
        const auto sz = m_doc->size(0);
        auto& d = m_cdrag;
        d.px = std::clamp((cx - r.x) / m_scale, 0.0, sz.w);
        d.py = std::clamp((cy - r.y) / m_scale, 0.0, sz.h);
        d.grip = core::picture::crop_hit(m_crop, d.px, d.py, 10 / m_scale);
        const bool whole = m_crop.w >= sz.w - 0.5 && m_crop.h >= sz.h - 0.5;
        if (d.grip == core::picture::Grip::Body && whole) d.grip = core::picture::Grip::None;
        d.start = d.before = m_crop;
        d.active = true;
        return;
    }
    const bool marks_ok = m_doc->kind() == core::Kind::Pdf;
    if (pg < 0 || !marks_ok) {
        if (m_sel.valid()) clear_mark();
        g->set_state(Gtk::EventSequenceState::DENIED);
        return;
    }
    const auto& r = m_placed.pages[std::size_t(pg)];
    const M::Point at{(cx - r.x) / m_scale, (cy - r.y) / m_scale};
    const auto b = m_doc->base(pg);
    const int t = m_doc->turn(pg);

    if (m_tool != Tool::Select) {   // a new mark
        g->set_state(Gtk::EventSequenceState::CLAIMED);
        gtk_widget_grab_focus(m_canvas);
        m_sel = {};
        m_drag.mode = DragMode::Create;
        m_drag.page = pg;
        m_drag.press = at;
        m_drag.live = M::boxed(core::MarkKind::Box, {at.x, at.y, 0, 0}, m_color, m_width, b.w, b.h, t);
        m_drag.live.w = m_drag.live.h = 0;   // invisible until it moves
        return;
    }

    const auto hit = M::hit(shapes(pg), cx, cy, 9);
    if (hit.index < 0) {
        if (m_sel.valid()) clear_mark();
        // A form field under the click (s011): the Shell fills it.
        const auto u = M::unturn_point(at, b.w, b.h, t);
        const auto& fl = page_fields(pg);
        if (const int f = core::forms::hit(fl, u.x, u.y); f >= 0) {
            g->set_state(Gtk::EventSequenceState::CLAIMED);
            gtk_widget_grab_focus(m_canvas);
            m_field.emit(pg, fl[std::size_t(f)]);
            return;
        }
        g->set_state(Gtk::EventSequenceState::DENIED);
        return;
    }
    g->set_state(Gtk::EventSequenceState::CLAIMED);
    gtk_widget_grab_focus(m_canvas);
    const auto list = m_doc->marks(pg);
    const auto& m = list[std::size_t(hit.index)];
    // A second press on the same note / text, quickly: open it to edit.
    const gint64 now = g_get_monotonic_time();
    const bool again = m_last_hit.page == pg && m_last_hit.index == hit.index && now - m_last_press < 400000;
    m_last_press = now;
    m_last_hit = {pg, hit.index};
    m_sel = {pg, hit.index};
    if (again && (m.kind == core::MarkKind::Note || m.kind == core::MarkKind::Text)) {
        m_last_press = 0;
        gtk_widget_queue_draw(m_canvas);
        m_mark_open.emit(pg, hit.index);
        return;
    }
    m_drag.page = pg;
    m_drag.press = at;
    m_drag.start = m_drag.live = m;
    m_drag.start_box = M::view_box(m, b.w, b.h, t);
    if (M::is_line(m.kind)) std::tie(m_drag.a0, m_drag.b0) = M::view_line(m, b.w, b.h, t);
    m_drag.mode = hit.part == M::Part::Corner ? DragMode::Resize
                : hit.part == M::Part::Start  ? DragMode::Start
                : hit.part == M::Part::End    ? DragMode::End
                                              : DragMode::Move;
    gtk_widget_queue_draw(m_canvas);
}

void DocView::drag_update(double dx, double dy) {
    if (m_cdrag.active && m_doc) {
        const auto sz = m_doc->size(0);
        const double ux = dx / m_scale, uy = dy / m_scale;
        auto& d = m_cdrag;
        if (d.grip == core::picture::Grip::None) {
            const double bx = std::clamp(d.px + ux, 0.0, sz.w), by = std::clamp(d.py + uy, 0.0, sz.h);
            m_crop = {std::min(d.px, bx), std::min(d.py, by), std::abs(bx - d.px), std::abs(by - d.py)};
        } else {
            m_crop = core::picture::crop_drag(d.start, d.grip, ux, uy, sz.w, sz.h, 8 / m_scale);
        }
        gtk_widget_queue_draw(m_canvas);
        m_crop_changed.emit();
        return;
    }
    if (m_drag.mode == DragMode::None || !m_doc || m_drag.page >= m_doc->count()) return;
    const int pg = m_drag.page;
    const auto b = m_doc->base(pg);
    const auto seen = m_doc->size(pg);
    const int t = m_doc->turn(pg);
    const double ux = dx / m_scale, uy = dy / m_scale;
    auto& d = m_drag;
    switch (d.mode) {
        case DragMode::Move: {
            const auto mv = core::sign::move(d.start_box, ux, uy, seen.w, seen.h);
            d.live = M::moved(d.start, mv.x - d.start_box.x, mv.y - d.start_box.y, t);
            break;
        }
        case DragMode::Resize: {
            const auto nb = d.start.kind == core::MarkKind::Signature
                                ? core::sign::resize(d.start_box, ux, uy, seen.w, seen.h)
                                : core::sign::resize_free(d.start_box, ux, uy, seen.w, seen.h);
            d.live = M::with_view_box(d.start, nb, b.w, b.h, t);
            break;
        }
        case DragMode::Start:
        case DragMode::End: {
            auto p = d.mode == DragMode::Start ? d.a0 : d.b0;
            const auto [px, py] = core::sign::clamp_point(p.x + ux, p.y + uy, seen.w, seen.h);
            p = {px, py};
            d.live = M::with_view_line(d.start, d.mode == DragMode::Start ? p : d.a0,
                                       d.mode == DragMode::End ? p : d.b0, b.w, b.h, t);
            break;
        }
        case DragMode::Create: {
            const auto [px, py] = core::sign::clamp_point(d.press.x + ux, d.press.y + uy, seen.w, seen.h);
            const M::Point z{px, py};
            switch (m_tool) {
                case Tool::Line:  d.live = M::line(core::MarkKind::Line, d.press, z, m_color, m_width, b.w, b.h, t); break;
                case Tool::Arrow: d.live = M::line(core::MarkKind::Arrow, d.press, z, m_color, m_width, b.w, b.h, t); break;
                case Tool::Ellipse:
                    d.live = M::boxed(core::MarkKind::Ellipse, M::span(d.press, z), m_color, m_width, b.w, b.h, t);
                    break;
                case Tool::Highlight:   // the box you sweep; it snaps to the text at release
                    d.live = M::highlight({}, M::span(d.press, z), m_color, b.w, b.h, t);
                    break;
                case Tool::Text:        // a dashed box shows the width you'll get
                case Tool::Box:
                    d.live = M::boxed(core::MarkKind::Box, M::span(d.press, z), m_color,
                                      m_tool == Tool::Text ? 1 : m_width, b.w, b.h, t);
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
    gtk_widget_queue_draw(m_canvas);
}

void DocView::drag_end() {
    if (m_cdrag.active) {
        m_cdrag.active = false;
        // A click, or a frame too small to see: the frame stays as it was.
        if (m_cdrag.grip == core::picture::Grip::None && (m_crop.w * m_scale < 6 || m_crop.h * m_scale < 6))
            m_crop = m_cdrag.before;
        gtk_widget_queue_draw(m_canvas);
        m_crop_changed.emit();
        return;
    }
    auto d = m_drag;
    m_drag.mode = DragMode::None;
    gtk_widget_queue_draw(m_canvas);
    if (d.mode == DragMode::None || !m_doc || d.page >= m_doc->count()) return;
    const int pg = d.page;
    const auto b = m_doc->base(pg);
    const auto seen = m_doc->size(pg);
    const int t = m_doc->turn(pg);
    if (d.mode != DragMode::Create) {
        if (d.live == d.start) return;
        auto m = d.live;
        if (m.kind == core::MarkKind::Text) {   // a text box is as tall as its text
            const double th = mark_art::text_height(m);
            (((m.turn % 2) + 2) % 2 ? m.w : m.h) = th;
        }
        m_mark_edit.emit(pg, m_sel.index, m, d.mode == DragMode::Move ? "Move" : "Resize");
        return;
    }
    const M::Point a = d.press;
    const auto box = M::view_box(d.live, b.w, b.h, t);
    const bool tiny = box.w < 4 && box.h < 4;
    core::Mark m;
    switch (m_tool) {
        case Tool::Note:
            m = M::note(a, "", m_color, b.w, b.h, t);
            break;
        case Tool::Text: {
            auto r = tiny || box.w < 24 ? core::layout::Rect{a.x, a.y, std::min(200.0, seen.w - a.x), 0} : box;
            r.w = std::max(r.w, 24.0);
            m = M::boxed(core::MarkKind::Text, {r.x, r.y, r.w, 20}, m_color, 1, b.w, b.h, t);
            break;
        }
        case Tool::Highlight: {
            // The text you swept, line by line -- poppler's selection, in the
            // unturned frame, turned to the seen frame for highlight().
            const auto ua = M::unturn_point({box.x, box.y}, b.w, b.h, t);
            const auto ub = M::unturn_point({box.x + box.w, box.y + box.h}, b.w, b.h, t);
            std::vector<core::layout::Rect> seen_parts;
            for (const auto& u : m_doc->text_rects(pg, ua, ub)) seen_parts.push_back(M::turn_rect(u, b.w, b.h, t));
            if (seen_parts.empty() && tiny) return;
            m = M::highlight(seen_parts, box, m_color, b.w, b.h, t);
            break;
        }
        case Tool::Line:
        case Tool::Arrow:
            if (std::hypot(d.live.w, d.live.h) < 4) return;
            m = d.live;
            break;
        case Tool::Box:
        case Tool::Ellipse:
            if (box.w < 4 || box.h < 4) return;
            m = d.live;
            break;
        default: return;
    }
    m_mark_create.emit(pg, m);
}

// The marks on page `page`, drawn live over its paper texture: highlights
// MULTIPLIED into the paper (text stays black under them), everything else
// on top, all by mark_art -- the same routine as thumbnails, export and Save.
// Then the selection: a frame and corner knob, or a line's two end knobs.
void DocView::snapshot_marks(GtkSnapshot* s, int page, const Rect_& r, double ox, double oy,
                             const Glib::RefPtr<Gdk::Texture>& paper, const graphene_rect_t& box) {
    const auto list = live_marks(page);
    const auto b = m_doc->base(page);
    const int t = m_doc->turn(page);
    bool lights = false, rest = false;
    for (const auto& m : list) (m.kind == core::MarkKind::Highlight ? lights : rest) = true;
    auto paint = [&](bool highlights) {
        cairo_t* cr = gtk_snapshot_append_cairo(s, &box);
        cairo_translate(cr, r.x - ox, r.y - oy);
        cairo_scale(cr, m_scale, m_scale);
        mark_art::turn_frame(cr, t, b.w, b.h);
        for (const auto& m : list)
            if ((m.kind == core::MarkKind::Highlight) == highlights)
                mark_art::draw(cr, m, mark_art::Notes::Draw, mark_art::Blend::Translucent);
        cairo_destroy(cr);
    };
    if (lights) {
        gtk_snapshot_push_blend(s, GSK_BLEND_MODE_MULTIPLY);
        gtk_snapshot_append_texture(s, paper->gobj(), &box);   // bottom: the paper
        gtk_snapshot_pop(s);
        // top: the highlights, opaque, so multiply gives "marker on paper"
        cairo_t* cr = gtk_snapshot_append_cairo(s, &box);
        cairo_translate(cr, r.x - ox, r.y - oy);
        cairo_scale(cr, m_scale, m_scale);
        mark_art::turn_frame(cr, t, b.w, b.h);
        for (const auto& m : list)
            if (m.kind == core::MarkKind::Highlight) mark_art::draw(cr, m, mark_art::Notes::Draw, mark_art::Blend::Multiply);
        cairo_destroy(cr);
        gtk_snapshot_pop(s);
    }
    if (rest) paint(false);

    // The live box of a text box being drawn: dashed, so it reads as "width".
    const bool text_draft = m_drag.mode == DragMode::Create && m_drag.page == page && m_tool == Tool::Text;
    if (!(m_sel.page == page && m_sel.index >= 0 && m_sel.index < int(list.size())) && !text_draft) return;
    const GdkRGBA line{0.20f, 0.65f, 0.68f, 0.95f};
    auto knob = [&](double x, double y) {
        graphene_rect_t k = GRAPHENE_RECT_INIT(float(x - 5), float(y - 5), 10, 10);
        GskRoundedRect kr;
        gsk_rounded_rect_init_from_rect(&kr, &k, 5);
        gtk_snapshot_push_rounded_clip(s, &kr);
        gtk_snapshot_append_color(s, &line, &k);
        gtk_snapshot_pop(s);
    };
    const auto& m = text_draft ? m_drag.live : list[std::size_t(m_sel.index)];
    if (M::is_line(m.kind)) {
        const auto [a, z] = M::view_line(m, b.w, b.h, t);
        knob(r.x - ox + a.x * m_scale, r.y - oy + a.y * m_scale);
        knob(r.x - ox + z.x * m_scale, r.y - oy + z.y * m_scale);
        return;
    }
    const auto v = M::view_box(m, b.w, b.h, t);
    const float x = float(r.x - ox + v.x * m_scale), y = float(r.y - oy + v.y * m_scale);
    const float w = float(v.w * m_scale), h = float(v.h * m_scale);
    graphene_rect_t frame = GRAPHENE_RECT_INIT(x - 2, y - 2, w + 4, h + 4);
    GskRoundedRect rr;
    gsk_rounded_rect_init_from_rect(&rr, &frame, 3);
    const float bw[4] = {1.5f, 1.5f, 1.5f, 1.5f};
    const GdkRGBA bc[4] = {line, line, line, line};
    gtk_snapshot_append_border(s, &rr, bw, bc);
    if (!text_draft && M::resizable(m.kind)) knob(x + w, y + h);
}

void DocView::set_crop(bool on) {
    m_crop_on = on && m_doc && m_doc->count() > 0;
    m_cdrag.active = false;
    if (m_crop_on) {
        const auto sz = m_doc->size(0);
        m_crop = {0, 0, sz.w, sz.h};
        gtk_widget_grab_focus(m_canvas);
    }
    gtk_widget_set_cursor_from_name(m_canvas, m_crop_on ? "crosshair" : nullptr);
    gtk_widget_queue_draw(m_canvas);
    m_crop_changed.emit();
}

// The crop frame: what's cut away dimmed, the frame, thirds, four corner knobs.
void DocView::snapshot_crop(GtkSnapshot* s, const Rect_& r, double ox, double oy) {
    const float px = float(r.x - ox), py = float(r.y - oy), pw = float(r.w), ph = float(r.h);
    const float x = px + float(m_crop.x * m_scale), y = py + float(m_crop.y * m_scale);
    const float w = float(m_crop.w * m_scale), h = float(m_crop.h * m_scale);
    const GdkRGBA dim{0, 0, 0, 0.55f}, white{1, 1, 1, 0.95f}, faint{1, 1, 1, 0.35f};
    const GdkRGBA knob{0.20f, 0.65f, 0.68f, 1.0f};
    auto fill = [&](float a, float b, float c, float d, const GdkRGBA& col) {
        if (c <= 0 || d <= 0) return;
        graphene_rect_t g = GRAPHENE_RECT_INIT(a, b, c, d);
        gtk_snapshot_append_color(s, &col, &g);
    };
    fill(px, py, pw, y - py, dim);                         // above
    fill(px, y + h, pw, py + ph - (y + h), dim);           // below
    fill(px, y, x - px, h, dim);                           // left
    fill(x + w, y, px + pw - (x + w), h, dim);             // right
    for (int k = 1; k < 3; ++k) {                          // thirds
        fill(x + w * float(k) / 3.0f, y, 1, h, faint);
        fill(x, y + h * float(k) / 3.0f, w, 1, faint);
    }
    fill(x, y, w, 1.5f, white);
    fill(x, y + h - 1.5f, w, 1.5f, white);
    fill(x, y, 1.5f, h, white);
    fill(x + w - 1.5f, y, 1.5f, h, white);
    const std::pair<float, float> corners[] = {{x, y}, {x + w, y}, {x, y + h}, {x + w, y + h}};
    for (const auto& [kx, ky] : corners) {
        graphene_rect_t k = GRAPHENE_RECT_INIT(kx - 6, ky - 6, 12, 12);
        GskRoundedRect kr;
        gsk_rounded_rect_init_from_rect(&kr, &k, 6);
        gtk_snapshot_push_rounded_clip(s, &kr);
        gtk_snapshot_append_color(s, &white, &k);
        gtk_snapshot_pop(s);
        graphene_rect_t in = GRAPHENE_RECT_INIT(kx - 4, ky - 4, 8, 8);
        gsk_rounded_rect_init_from_rect(&kr, &in, 4);
        gtk_snapshot_push_rounded_clip(s, &kr);
        gtk_snapshot_append_color(s, &knob, &in);
        gtk_snapshot_pop(s);
    }
}

int DocView::count() const { return m_doc ? m_doc->count() : 0; }

void DocView::set_document(std::shared_ptr<Document> doc) {
    m_doc = std::move(doc);
    m_fields.clear();
    m_focus_page = m_focus_id = -1;
    m_crop_on = false;
    m_cdrag.active = false;
    m_sel = {};
    m_drag.mode = DragMode::None;
    m_cache.clear();
    m_hits.clear();
    m_hit = -1;
    m_sizes.clear();
    if (m_doc)
        for (int i = 0; i < m_doc->count(); ++i) m_sizes.push_back(m_doc->size(i));
    // Paper fits the width; a picture fits the window but is never blown up
    // past 100% by a fit (relayout).
    m_page = m_doc ? 0 : -1;
    // Paged: the page fitted to the window (Scott, s005). Continuous: width.
    m_fit = m_paged || (m_doc && m_doc->kind() == core::Kind::Image) ? Fit::Page : Fit::Width;
    relayout();
    gtk_adjustment_set_value(canvas(m_canvas)->vadj, 0);
    gtk_adjustment_set_value(canvas(m_canvas)->hadj, 0);
    gtk_widget_queue_draw(m_canvas);
    gtk_widget_grab_focus(m_canvas);
    update_page();
    m_changed.emit();
}

// ── form fields (s011) ──────────────────────────────────────────────────────
const std::vector<core::Field>& DocView::page_fields(int page) const {
    static const std::vector<core::Field> none;
    if (!m_doc || page < 0 || page >= m_doc->count() || m_doc->kind() != core::Kind::Pdf) return none;
    auto it = m_fields.find(page);
    if (it == m_fields.end()) it = m_fields.emplace(page, m_doc->fields(page)).first;
    return it->second;
}

void DocView::set_field_focus(int page, int id) {
    m_focus_page = page;
    m_focus_id = id;
    gtk_widget_queue_draw(m_canvas);
}

core::layout::Rect DocView::field_screen_box(int page, const core::Field& f) const {
    if (!m_doc || page < 0 || page >= int(m_placed.pages.size())) return {};
    const auto& r = m_placed.pages[std::size_t(page)];
    const auto b = m_doc->base(page);
    const auto v = M::turn_rect(f.box, b.w, b.h, m_doc->turn(page));
    auto* c = canvas(m_canvas);
    return {r.x + v.x * m_scale - gtk_adjustment_get_value(c->hadj), r.y + v.y * m_scale - gtk_adjustment_get_value(c->vadj),
            v.w * m_scale, v.h * m_scale};
}

// A pale blue over every field you can fill (as other readers do), so a form
// shows where to type; the one being edited gets an outline.
void DocView::snapshot_fields(GtkSnapshot* s, int page, const core::layout::Rect& r, double ox, double oy) {
    const auto& fl = page_fields(page);
    if (fl.empty()) return;
    const auto b = m_doc->base(page);
    const int t = m_doc->turn(page);
    for (const auto& f : fl) {
        if (!core::forms::fillable(f)) continue;
        const auto v = M::turn_rect(f.box, b.w, b.h, t);
        graphene_rect_t fr = GRAPHENE_RECT_INIT(float(r.x - ox + v.x * m_scale), float(r.y - oy + v.y * m_scale),
                                                float(v.w * m_scale), float(v.h * m_scale));
        const GdkRGBA tint{0.21f, 0.52f, 0.89f, 0.12f};
        gtk_snapshot_append_color(s, &tint, &fr);
        const bool focus = page == m_focus_page && f.id == m_focus_id;
        const GdkRGBA edge = focus ? GdkRGBA{0.21f, 0.52f, 0.89f, 0.95f} : GdkRGBA{0.21f, 0.52f, 0.89f, 0.35f};
        const float w = focus ? 2.0f : 1.0f;
        GskRoundedRect rr;
        gsk_rounded_rect_init_from_rect(&rr, &fr, 0);
        const float widths[4] = {w, w, w, w};
        const GdkRGBA colors[4] = {edge, edge, edge, edge};
        gtk_snapshot_append_border(s, &rr, widths, colors);
    }
}

void DocView::refresh(std::shared_ptr<Document> doc, int page) {
    m_doc = std::move(doc);
    m_fields.clear();
    m_cdrag.active = false;
    if (m_crop_on && m_doc && m_doc->count() > 0) {   // the picture changed under the frame: all of it
        const auto sz = m_doc->size(0);
        m_crop = {0, 0, sz.w, sz.h};
    }
    m_drag.mode = DragMode::None;
    if (!m_doc || m_sel.page >= m_doc->count() || m_sel.index >= int(m_doc->marks(std::max(0, m_sel.page)).size()))
        m_sel = {};
    m_cache.clear();
    m_hits.clear();
    m_hit = -1;
    m_sizes.clear();
    if (m_doc)
        for (int i = 0; i < m_doc->count(); ++i) m_sizes.push_back(m_doc->size(i));
    m_page = m_doc ? std::clamp(page, 0, std::max(0, m_doc->count() - 1)) : -1;
    relayout();
    if (m_doc) go_to(m_page);
    gtk_widget_queue_draw(m_canvas);
    m_changed.emit();
}

void DocView::canvas_allocate(int w, int h) {
    if (w == m_vw && h == m_vh) { configure_adjustments(); return; }
    // Keep the middle of the view where it was across a resize.
    const int keep = m_page;
    m_vw = w;
    m_vh = h;
    relayout();
    if (m_fit != Fit::None && keep >= 0) go_to(keep);
}

void DocView::relayout() {
    if (m_doc) {
        if (m_fit == Fit::Width) m_scale = L::fit_width(m_sizes, m_vw, m_mode);
        else if (m_fit == Fit::Page) {
            m_scale = L::fit_page(m_sizes, std::max(m_page, 0), m_vw, m_vh, m_mode);
            if (m_doc->kind() == core::Kind::Image) m_scale = std::min(m_scale, 1.0);
        }
    }
    m_placed = m_paged ? L::place_row(m_sizes, std::max(m_page, 0), m_scale, m_vw, m_vh, m_mode)
                       : L::place(m_sizes, m_scale, m_vw, m_mode);
    configure_adjustments();
}

void DocView::configure_adjustments() {
    auto* c = canvas(m_canvas);
    const double vh = std::max(m_placed.height, double(m_vh));
    gtk_adjustment_configure(c->vadj, std::clamp(gtk_adjustment_get_value(c->vadj), 0.0, std::max(0.0, vh - m_vh)),
                             0, vh, 48, m_vh * 0.9, m_vh);
    const double vw = std::max(m_placed.width, double(m_vw));
    gtk_adjustment_configure(c->hadj, std::clamp(gtk_adjustment_get_value(c->hadj), 0.0, std::max(0.0, vw - m_vw)),
                             0, vw, 48, m_vw * 0.9, m_vw);
}

void DocView::set_scale(double s, Fit fit, double ax, double ay) {
    if (!m_doc) return;
    s = L::clamp_scale(s);
    auto* c = canvas(m_canvas);
    // The content point under the anchor, as a fraction of its page, stays put.
    const double cx = gtk_adjustment_get_value(c->hadj) + ax;
    const double cy = gtk_adjustment_get_value(c->vadj) + ay;
    const int pi = L::page_at(m_placed, cy);
    double fx = 0.5, fy = 0.0;
    if (pi >= 0) {
        const auto& r = m_placed.pages[std::size_t(pi)];
        fx = r.w > 0 ? (cx - r.x) / r.w : 0.5;
        fy = r.h > 0 ? (cy - r.y) / r.h : 0.0;
    }
    m_fit = fit;
    m_scale = s;
    relayout();   // a fit mode may override s
    if (pi >= 0) {
        const auto& r = m_placed.pages[std::size_t(pi)];
        gtk_adjustment_set_value(c->hadj, r.x + fx * r.w - ax);
        gtk_adjustment_set_value(c->vadj, r.y + fy * r.h - ay);
    }
    gtk_widget_queue_draw(m_canvas);
    update_page();
    m_changed.emit();
}

void DocView::zoom_in()     { set_scale(L::zoom_in(m_scale),  Fit::None, m_vw / 2.0, m_vh / 2.0); }
void DocView::zoom_out()    { set_scale(L::zoom_out(m_scale), Fit::None, m_vw / 2.0, m_vh / 2.0); }
void DocView::zoom_actual() { set_scale(1.0, Fit::None, m_vw / 2.0, m_vh / 2.0); }
void DocView::fit_width()   { set_scale(m_scale, Fit::Width, m_vw / 2.0, m_vh / 2.0); }
void DocView::fit_page() {
    const int p = std::max(m_page, 0);
    set_scale(m_scale, Fit::Page, m_vw / 2.0, m_vh / 2.0);
    go_to(p);
}

void DocView::go_to(int page) {
    if (!m_doc || m_placed.pages.empty()) return;
    page = std::clamp(page, 0, int(m_placed.pages.size()) - 1);
    if (m_paged) {
        // Show that page's row alone; the zoom (or the fit) carries over.
        m_page = L::first_of_row(L::row_of(page, m_mode), m_mode);
        relayout();
        gtk_adjustment_set_value(canvas(m_canvas)->vadj, 0);
        auto* h = canvas(m_canvas)->hadj;
        gtk_adjustment_set_value(h, (gtk_adjustment_get_upper(h) - gtk_adjustment_get_page_size(h)) / 2);
        m_flip = 0;
        gtk_widget_queue_draw(m_canvas);
        m_changed.emit();
        return;
    }
    // The row's top: in a spread the taller page sets it.
    Rect_ r = m_placed.pages[std::size_t(page)];
    for (const int other : {page - 1, page + 1})
        if (other >= 0 && other < int(m_placed.pages.size()) &&
            L::row_of(other, m_mode) == L::row_of(page, m_mode)) {
            const auto& o = m_placed.pages[std::size_t(other)];
            const double top = std::min(r.y, o.y), bot = std::max(r.y + r.h, o.y + o.h);
            r.y = top;
            r.h = bot - top;
        }
    // Fit page: centre it; otherwise its top a margin below the view's top.
    const double y = m_fit == Fit::Page ? r.y + r.h / 2 - m_vh / 2.0 : r.y - L::kMargin;
    gtk_adjustment_set_value(canvas(m_canvas)->vadj, y);
    m_page = page;
    m_changed.emit();
}

// A page at a time -- or, in Book view, a spread at a time.
void DocView::next_page() { go_to(L::step(m_page, +1, count(), m_mode)); }
void DocView::prev_page() { go_to(L::step(m_page, -1, count(), m_mode)); }

void DocView::set_paged(bool on) {
    if (on == m_paged) return;
    const int keep = std::max(m_page, 0);
    m_paged = on;
    if (m_fit != Fit::None) m_fit = on ? Fit::Page : Fit::Width;
    relayout();
    if (m_doc) go_to(keep);
    gtk_widget_queue_draw(m_canvas);
    m_changed.emit();
}

bool DocView::at_top() const {
    return gtk_adjustment_get_value(canvas(m_canvas)->vadj) <= 0.5;
}

bool DocView::at_bottom() const {
    auto* v = canvas(m_canvas)->vadj;
    return gtk_adjustment_get_value(v) + gtk_adjustment_get_page_size(v) >= gtk_adjustment_get_upper(v) - 0.5;
}

// Paged: to the next / previous row. Going back lands at the bottom of the
// earlier page (you were reading down into this one), forward at the top.
void DocView::turn(int dir, bool to_end) {
    const int target = L::step(m_page, dir, count(), m_mode);
    if (target == m_page) return;
    go_to(target);
    if (to_end) {
        auto* v = canvas(m_canvas)->vadj;
        gtk_adjustment_set_value(v, gtk_adjustment_get_upper(v));
    }
}

void DocView::set_book(bool on) {
    const auto mode = on ? L::Mode::Book : L::Mode::Continuous;
    if (mode == m_mode) return;
    const int keep = std::max(m_page, 0);
    m_mode = mode;
    relayout();
    if (m_doc) go_to(L::first_of_row(L::row_of(keep, m_mode), m_mode));
    gtk_widget_queue_draw(m_canvas);
    m_changed.emit();
}

void DocView::canvas_scrolled() { update_page(); }

void DocView::update_page() {
    if (!m_doc) { m_page = -1; return; }
    if (m_paged) return;   // scrolling within a page never changes which page it is
    const double mid = gtk_adjustment_get_value(canvas(m_canvas)->vadj) + m_vh / 2.0;
    const int p = L::page_at(m_placed, mid);
    if (p != m_page) {
        m_page = p;
        m_changed.emit();
    }
}

Glib::RefPtr<Gdk::Texture> DocView::render_page(int i, double s) {
    return render_texture(*m_doc, i, s, kMaxPixels, false);   // marks go on top, live
}

void DocView::set_hits(std::vector<core::search::Hit> hits) {
    m_hits = std::move(hits);
    if (m_hit >= int(m_hits.size())) m_hit = -1;
    gtk_widget_queue_draw(m_canvas);
}

void DocView::show_hit(int index) {
    m_hit = index;
    if (m_doc && index >= 0 && index < int(m_hits.size())) {
        const auto& h = m_hits[std::size_t(index)];
        if (m_paged && L::row_of(h.page, m_mode) != L::row_of(m_page, m_mode)) go_to(h.page);
        const auto& pr = m_placed.pages[std::size_t(h.page)];
        const double x = pr.x + h.r.x * m_scale, y = pr.y + h.r.y * m_scale;
        const double w = h.r.w * m_scale, hh = h.r.h * m_scale;
        auto* c = canvas(m_canvas);
        gtk_adjustment_set_value(c->vadj, y + hh / 2 - m_vh / 2.0);
        const double ox = gtk_adjustment_get_value(c->hadj);
        if (x < ox || x + w > ox + m_vw) gtk_adjustment_set_value(c->hadj, x + w / 2 - m_vw / 2.0);
        update_page();
    }
    gtk_widget_queue_draw(m_canvas);
}

void DocView::evict(int first, int last) {
    if (m_cache.size() <= kMaxCached) return;
    for (auto it = m_cache.begin(); it != m_cache.end() && m_cache.size() > kMaxCached;) {
        if (it->first < first - 3 || it->first > last + 3) it = m_cache.erase(it);
        else ++it;
    }
}

void DocView::canvas_snapshot(GtkSnapshot* s, int w, int h) {
    if (!m_doc) return;
    auto* c = canvas(m_canvas);
    const double ox = gtk_adjustment_get_value(c->hadj);
    const double oy = gtk_adjustment_get_value(c->vadj);
    const auto [first, last] = L::visible(m_placed, oy, oy + h);
    if (first < 0) return;
    const double sf = gtk_widget_get_scale_factor(m_canvas);
    const double want = m_scale * sf;
    // Clip to the canvas: GTK's overflow only clips CHILDREN, never a widget's
    // own snapshot, and a zoomed page would paint over the sidebar.
    graphene_rect_t clip = GRAPHENE_RECT_INIT(0, 0, float(w), float(h));
    gtk_snapshot_push_clip(s, &clip);
    for (int i = first; i <= last; ++i) {
        const auto& r = m_placed.pages[std::size_t(i)];
        graphene_rect_t box = GRAPHENE_RECT_INIT(float(r.x - ox), float(r.y - oy), float(r.w), float(r.h));
        // A soft shadow under paper. Not under a picture with transparency:
        // it would show through the clear parts as a dark box.
        if (m_doc->opaque()) {
            GskShadow sh{{0, 0, 0, 0.45f}, 0, 3, 10};
            GskRoundedRect rr;
            gsk_rounded_rect_init_from_rect(&rr, &box, 2);
            gtk_snapshot_append_outset_shadow(s, &rr, &sh.color, sh.dx, sh.dy, 0, sh.radius);
        }
        auto it = m_cache.find(i);
        const bool stale = it == m_cache.end() || std::abs(it->second.scale - want) > 1e-6;
        if (stale && !(m_live && it != m_cache.end())) {
            m_cache[i] = {render_page(i, want), want};
            it = m_cache.find(i);
        }
        gtk_snapshot_append_texture(s, it->second.tex->gobj(), &box);
        snapshot_fields(s, i, r, ox, oy);
        snapshot_marks(s, i, r, ox, oy, it->second.tex, box);
        if (m_crop_on && i == 0) snapshot_crop(s, r, ox, oy);

        // Search hits on this page: all tinted, the current one stronger.
        auto lo = std::lower_bound(m_hits.begin(), m_hits.end(), i,
                                   [](const core::search::Hit& h, int p) { return h.page < p; });
        for (auto hit = lo; hit != m_hits.end() && hit->page == i; ++hit) {
            const bool cur = int(hit - m_hits.begin()) == m_hit;
            const GdkRGBA tint = cur ? GdkRGBA{1.0f, 0.55f, 0.10f, 0.55f} : GdkRGBA{1.0f, 0.85f, 0.20f, 0.38f};
            graphene_rect_t hr = GRAPHENE_RECT_INIT(
                float(r.x - ox + (hit->r.x - 1) * m_scale), float(r.y - oy + (hit->r.y - 1) * m_scale),
                float((hit->r.w + 2) * m_scale), float((hit->r.h + 2) * m_scale));
            gtk_snapshot_append_color(s, &tint, &hr);
        }
    }
    gtk_snapshot_pop(s);
    evict(first, last);
}

}  // namespace lookit
