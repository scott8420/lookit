#include "DocView.hpp"
#include "Log.hpp"

#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/eventcontrollerscroll.h>
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
    motion->signal_motion().connect([this](double x, double y) { m_mx = x; m_my = y; });
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
        const bool shift = (st & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
        if ((st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK)) != Gdk::ModifierType{})
            return false;
        auto* v = canvas(m_canvas)->vadj;
        const double page = gtk_adjustment_get_page_size(v);
        auto by = [&](double d) {
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
            case GDK_KEY_Right:     if (gtk_adjustment_get_upper(canvas(m_canvas)->hadj) >
                                        gtk_adjustment_get_page_size(canvas(m_canvas)->hadj) + 1) return false;
                                    next_page(); return true;
            case GDK_KEY_Left:      if (gtk_adjustment_get_upper(canvas(m_canvas)->hadj) >
                                        gtk_adjustment_get_page_size(canvas(m_canvas)->hadj) + 1) return false;
                                    prev_page(); return true;
            default:                return false;
        }
    }, false);
    cw->add_controller(keys);
}

DocView::~DocView() {
    if (m_canvas) g_object_set_data(G_OBJECT(m_canvas), "lookit-view", nullptr);
}

int DocView::count() const { return m_doc ? m_doc->count() : 0; }

void DocView::set_document(std::shared_ptr<Document> doc) {
    m_doc = std::move(doc);
    m_cache.clear();
    m_sizes.clear();
    if (m_doc)
        for (int i = 0; i < m_doc->count(); ++i) m_sizes.push_back(m_doc->size(i));
    // Paper fits the width; a picture fits the window but is never blown up
    // past 100% by a fit (relayout).
    m_page = m_doc ? 0 : -1;
    m_fit = m_doc && m_doc->kind() == core::Kind::Image ? Fit::Page : Fit::Width;
    relayout();
    gtk_adjustment_set_value(canvas(m_canvas)->vadj, 0);
    gtk_adjustment_set_value(canvas(m_canvas)->hadj, 0);
    gtk_widget_queue_draw(m_canvas);
    gtk_widget_grab_focus(m_canvas);
    update_page();
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
    m_placed = L::place(m_sizes, m_scale, m_vw, m_mode);
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
    const double mid = gtk_adjustment_get_value(canvas(m_canvas)->vadj) + m_vh / 2.0;
    const int p = L::page_at(m_placed, mid);
    if (p != m_page) {
        m_page = p;
        m_changed.emit();
    }
}

Glib::RefPtr<Gdk::Texture> DocView::render_page(int i, double s) {
    const auto& sz = m_sizes[std::size_t(i)];
    double rs = s;
    if (sz.w * sz.h * rs * rs > kMaxPixels) rs = std::sqrt(kMaxPixels / (sz.w * sz.h));
    const int w = std::max(1, int(std::ceil(sz.w * rs)));
    const int h = std::max(1, int(std::ceil(sz.h * rs)));
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    m_doc->render(cr, i, rs);
    cairo_destroy(cr);
    cairo_surface_flush(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    GBytes* bytes = g_bytes_new(cairo_image_surface_get_data(surf), std::size_t(stride) * std::size_t(h));
    cairo_surface_destroy(surf);
    // GDK_MEMORY_DEFAULT is premultiplied BGRA (little-endian) -- Cairo's ARGB32.
    GdkTexture* t = gdk_memory_texture_new(w, h, GDK_MEMORY_DEFAULT, bytes, std::size_t(stride));
    g_bytes_unref(bytes);
    return Glib::wrap(t);
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
    }
    gtk_snapshot_pop(s);
    evict(first, last);
}

}  // namespace lookit
