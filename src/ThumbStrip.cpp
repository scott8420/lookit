#include "ThumbStrip.hpp"
#include "Log.hpp"

#include <gtkmm/box.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gdkmm/contentprovider.h>
#include <glibmm/main.h>
#include <gtkmm/label.h>
#include <gtkmm/listitem.h>
#include <gtkmm/picture.h>
#include <gtkmm/signallistitemfactory.h>

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <set>
#include <mutex>
#include <thread>

namespace lookit {

// ── The thumbnail thread (s014.2) ───────────────────────────────────────────
// A scanned book can take half a second a page (JPEG 2000 is decoded whole,
// however small it is drawn), and the strip binds dozens of rows at once: drawn
// on the main thread that froze the window for minutes. Now a row shows an
// empty page of the right shape at once and a thread draws its thumbnail from
// the document's twin (Document::twin), the rows nearest what's on screen first
// (GTK keeps ~200 rows bound around it). Rows scrolled away are dropped from
// the queue. A finished thumbnail comes back to the main
// thread through an idle callback; one for an old document is thrown away.
struct ThumbStrip::Job {
    std::mutex mu;
    std::condition_variable cv;
    std::set<int> want;                    // pages to draw
    int focus = 0;                         // the page on screen: nearest goes first
    bool stop = false;
    bool done = false;                     // the thread has returned
    double sf = 1.0;                       // the window's scale factor
    std::unique_ptr<Document> doc;         // the twin: this thread's alone
    ThumbStrip* owner = nullptr;           // main thread only; null once dropped
};

namespace {

struct Drawn {
    std::shared_ptr<ThumbStrip::Job> job;
    int page = 0, w = 0, h = 0, stride = 0;
    GBytes* bytes = nullptr;
};

}  // namespace

void ThumbStrip::run(std::shared_ptr<Job> job, int width) {
    for (;;) {
        int page = 0;
        double sf = 1.0;
        {
            std::unique_lock lock(job->mu);
            job->cv.wait(lock, [&] { return job->stop || !job->want.empty(); });
            if (job->stop) break;
            // The wanted page nearest the one on screen.
            auto it = job->want.lower_bound(job->focus);
            if (it == job->want.end() || (it != job->want.begin() &&
                                          job->focus - *std::prev(it) < *it - job->focus))
                it = it == job->want.begin() ? it : std::prev(it);
            page = *it;
            job->want.erase(it);
            sf = job->sf;
        }
        const Document& doc = *job->doc;
        if (page < 0 || page >= doc.count()) continue;
        const auto sz = doc.size(page);
        double rs = sz.w > 0 ? width * sf / sz.w : 1.0;
        if (sz.w * sz.h * rs * rs > 1.0e6) rs = std::sqrt(1.0e6 / (sz.w * sz.h));
        const int w = std::max(1, int(std::ceil(sz.w * rs)));
        const int h = std::max(1, int(std::ceil(sz.h * rs)));
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        cairo_t* cr = cairo_create(surf);
        doc.render(cr, page, rs, true);
        cairo_destroy(cr);
        cairo_surface_flush(surf);
        auto* d = new Drawn{job, page, w, h, cairo_image_surface_get_stride(surf), nullptr};
        d->bytes = g_bytes_new(cairo_image_surface_get_data(surf), std::size_t(d->stride) * std::size_t(h));
        cairo_surface_destroy(surf);
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, [](gpointer data) -> gboolean {
            std::unique_ptr<Drawn> d(static_cast<Drawn*>(data));
            if (d->job->owner) {
                GdkTexture* t = gdk_memory_texture_new(d->w, d->h, GDK_MEMORY_DEFAULT, d->bytes, std::size_t(d->stride));
                d->job->owner->deliver(d->page, Glib::wrap(t));
            }
            g_bytes_unref(d->bytes);
            return G_SOURCE_REMOVE;
        }, d, nullptr);
    }
    std::lock_guard lock(job->mu);
    job->doc.reset();   // the twin's popplers go on this thread, off the window's
    job->done = true;
}

ThumbStrip::~ThumbStrip() {
    drop_job();
    for (auto& [job, t] : m_threads) if (t.joinable()) t.join();   // at most one page each
}

void ThumbStrip::drop_job() {
    if (m_idle) { m_idle.disconnect(); }
    m_idle_want.clear();
    if (!m_job) return;
    {
        std::lock_guard lock(m_job->mu);
        m_job->stop = true;
        m_job->want.clear();
    }
    m_job->owner = nullptr;
    m_job->cv.notify_all();
    m_job.reset();
}

void ThumbStrip::request(int page) {
    if (m_job) {
        {
            std::lock_guard lock(m_job->mu);
            m_job->want.insert(page);
            m_job->focus = focus();
            m_job->sf = std::max(1, get_scale_factor());
        }
        m_job->cv.notify_one();
        return;
    }
    // No twin (DjVu, books, pictures): one page per idle turn on this thread,
    // so the window still answers between pages.
    m_idle_want.erase(std::remove(m_idle_want.begin(), m_idle_want.end(), page), m_idle_want.end());
    m_idle_want.push_front(page);
    if (!m_idle)
        m_idle = Glib::signal_idle().connect([this] {
            if (m_idle_want.empty()) return false;
            const int p = m_idle_want.front();
            m_idle_want.pop_front();
            if (auto t = thumb(p)) deliver(p, t);
            return !m_idle_want.empty();
        });
}

int ThumbStrip::focus() const {
    // The page at the middle of the strip, from how far down it is scrolled.
    const int n = int(m_pages->get_n_items());
    const auto adj = const_cast<ThumbStrip*>(this)->get_vadjustment();
    if (n <= 0 || !adj || adj->get_upper() <= 0) return 0;
    const double mid = adj->get_value() + adj->get_page_size() / 2.0;
    return std::clamp(int(mid / adj->get_upper() * n), 0, n - 1);
}

void ThumbStrip::forget(int page) {
    if (m_job) {
        std::lock_guard lock(m_job->mu);
        m_job->want.erase(page);
    }
    m_idle_want.erase(std::remove(m_idle_want.begin(), m_idle_want.end(), page), m_idle_want.end());
}

void ThumbStrip::deliver(int page, const Glib::RefPtr<Gdk::Texture>& t) {
    keep(page, t);
    if (auto it = m_bound.find(page); it != m_bound.end()) show(it->second, t);
}

void ThumbStrip::show(Gtk::Picture* pic, const Glib::RefPtr<Gdk::Texture>& t) {
    pic->set_paintable(t);
    // Reserve the page's shape so rows don't jump as thumbnails arrive.
    if (t) pic->set_size_request(kWidth, int(kWidth * double(t->get_height()) / t->get_width()));
}

ThumbStrip::ThumbStrip() {
    set_name("view.thumbs");
    add_css_class("thumbs");
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_size_request(kWidth + 44, -1);

    m_pages = Gtk::StringList::create({});
    m_sel = Gtk::SingleSelection::create(m_pages);
    m_sel->set_autoselect(false);
    m_sel->set_can_unselect(true);

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
        box->add_css_class("thumb");
        auto pic = Gtk::make_managed<Gtk::Picture>();
        pic->set_can_shrink(true);
        pic->set_content_fit(Gtk::ContentFit::CONTAIN);
        pic->set_size_request(kWidth, -1);
        pic->add_css_class("thumb-pic");
        auto num = Gtk::make_managed<Gtk::Label>();
        num->add_css_class("thumb-num");
        box->append(*pic);
        box->append(*num);
        item->set_child(*box);

        // Drag a thumbnail: the payload is its position (set at bind).
        auto drag = Gtk::DragSource::create();
        drag->set_actions(Gdk::DragAction::MOVE);
        drag->signal_prepare().connect([this, box](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
            if (!m_editable) return {};
            Glib::Value<int> v;
            v.init(Glib::Value<int>::value_type());
            v.set(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box->gobj()), "lookit-pos")));
            return Gdk::ContentProvider::create(v);
        }, false);
        drag->signal_drag_begin().connect([box](const Glib::RefPtr<Gdk::Drag>&) { box->add_css_class("dragging"); });
        drag->signal_drag_end().connect([box](const Glib::RefPtr<Gdk::Drag>&, bool) { box->remove_css_class("dragging"); });
        box->add_controller(drag);

        // Drop on a thumbnail: before it (top half) or after it (bottom half).
        // Takes a moved thumbnail or files from outside (PDFs, pictures).
        auto drop = Gtk::DropTarget::create(G_TYPE_INVALID, Gdk::DragAction::MOVE | Gdk::DragAction::COPY);
        drop->set_gtypes({Glib::Value<int>::value_type(), GDK_TYPE_FILE_LIST});
        auto mark = [box](double y) {
            const bool after = y > box->get_height() / 2.0;
            box->remove_css_class(after ? "drop-before" : "drop-after");
            box->add_css_class(after ? "drop-after" : "drop-before");
        };
        drop->signal_enter().connect([this, mark](double, double y) {
            if (!m_editable) return Gdk::DragAction{};
            mark(y);
            return Gdk::DragAction::MOVE;
        }, false);
        drop->signal_motion().connect([this, mark](double, double y) {
            if (!m_editable) return Gdk::DragAction{};
            mark(y);
            return Gdk::DragAction::MOVE;
        }, false);
        drop->signal_leave().connect([box] { box->remove_css_class("drop-before"); box->remove_css_class("drop-after"); });
        drop->signal_drop().connect([this, box](const Glib::ValueBase& value, double, double y) {
            box->remove_css_class("drop-before");
            box->remove_css_class("drop-after");
            if (!m_editable) return false;
            const int at = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(box->gobj()), "lookit-pos")) +
                           (y > box->get_height() / 2.0 ? 1 : 0);
            if (G_VALUE_HOLDS_INT(value.gobj())) {
                const int from = g_value_get_int(value.gobj());
                Glib::signal_idle().connect_once([this, from, at] { m_move.emit(from, at); });
                return true;
            }
            if (G_VALUE_HOLDS(value.gobj(), GDK_TYPE_FILE_LIST)) {
                std::vector<std::string> paths;
                GSList* files = gdk_file_list_get_files(static_cast<GdkFileList*>(g_value_get_boxed(value.gobj())));
                for (GSList* l = files; l; l = l->next)
                    if (char* p = g_file_get_path(G_FILE(l->data))) { paths.push_back(p); g_free(p); }
                g_slist_free(files);
                if (paths.empty()) return false;
                Glib::signal_idle().connect_once([this, paths, at] { m_files.emit(paths, at); });
                return true;
            }
            return false;
        }, false);
        box->add_controller(drop);
    });
    factory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        if (!box) return;
        const int page = int(item->get_position());
        g_object_set_data(G_OBJECT(box->gobj()), "lookit-pos", GINT_TO_POINTER(page));
        auto* pic = dynamic_cast<Gtk::Picture*>(box->get_first_child());
        auto* num = dynamic_cast<Gtk::Label*>(pic ? pic->get_next_sibling() : nullptr);
        if (pic) {
            m_bound[page] = pic;
            if (auto it = m_cache.find(page); it != m_cache.end()) {
                show(pic, it->second);
            } else {
                // An empty page of the right shape now; the picture follows.
                pic->set_paintable({});
                const auto sz = m_doc ? m_doc->size(page) : core::layout::Size{1, 1};
                pic->set_size_request(kWidth, sz.w > 0 ? int(kWidth * sz.h / sz.w) : kWidth);
                request(page);
            }
        }
        if (num) num->set_text(std::to_string(page + 1));
    });
    factory->signal_unbind().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* box = dynamic_cast<Gtk::Box*>(item->get_child());
        auto* pic = box ? dynamic_cast<Gtk::Picture*>(box->get_first_child()) : nullptr;
        const int page = GPOINTER_TO_INT(box ? g_object_get_data(G_OBJECT(box->gobj()), "lookit-pos") : nullptr);
        if (auto it = m_bound.find(page); it != m_bound.end() && it->second == pic) {
            m_bound.erase(it);
            forget(page);
        }
    });
    m_list.set_factory(factory);
    m_list.set_model(m_sel);
    m_list.add_css_class("thumb-list");
    m_list.set_single_click_activate(false);
    set_child(m_list);

    // Scrolling the strip moves which thumbnails the thread draws first.
    get_vadjustment()->signal_value_changed().connect([this] {
        if (!m_job) return;
        std::lock_guard lock(m_job->mu);
        m_job->focus = focus();
    });

    m_sel->property_selected().signal_changed().connect([this] {
        if (m_syncing) return;
        const auto p = m_sel->get_selected();
        if (p != GTK_INVALID_LIST_POSITION) m_pick.emit(int(p));
    });
}

void ThumbStrip::set_document(std::shared_ptr<Document> doc) {
    drop_job();
    // Threads of documents gone by that have finished: join them now.
    for (auto it = m_threads.begin(); it != m_threads.end();) {
        if (it->first->done && it->second.joinable()) { it->second.join(); it = m_threads.erase(it); }
        else ++it;
    }
    m_doc = std::move(doc);
    m_cache.clear();
    m_bound.clear();
    if (m_doc)
        if (auto twin = m_doc->twin()) {
            m_job = std::make_shared<Job>();
            m_job->doc = std::move(twin);
            m_job->owner = this;
            m_job->sf = std::max(1, get_scale_factor());
            m_threads.emplace_back(m_job, std::thread(&ThumbStrip::run, m_job, kWidth));
        }
    m_syncing = true;
    const int old = int(m_pages->get_n_items());
    std::vector<Glib::ustring> names;
    const int n = m_doc ? m_doc->count() : 0;
    names.reserve(std::size_t(n));
    for (int i = 0; i < n; ++i) names.push_back(std::to_string(i + 1));
    m_pages->splice(0, guint(old), names);
    m_sel->set_selected(n > 0 ? 0 : GTK_INVALID_LIST_POSITION);
    m_syncing = false;
}

void ThumbStrip::set_current(int page) {
    if (page < 0 || page >= int(m_pages->get_n_items())) return;
    if (int(m_sel->get_selected()) == page) return;
    m_syncing = true;
    m_sel->set_selected(guint(page));
    gtk_list_view_scroll_to(m_list.gobj(), guint(page), GTK_LIST_SCROLL_NONE, nullptr);
    m_syncing = false;
}

Glib::RefPtr<Gdk::Texture> ThumbStrip::thumb(int page) {
    if (!m_doc || page < 0 || page >= m_doc->count()) return {};
    if (auto it = m_cache.find(page); it != m_cache.end()) return it->second;
    const auto sz = m_doc->size(page);
    const double sf = std::max(1, get_scale_factor());
    const double scale = sz.w > 0 ? kWidth * sf / sz.w : 1.0;
    auto t = render_texture(*m_doc, page, scale, 1.0e6);
    keep(page, t);
    return t;
}

void ThumbStrip::keep(int page, const Glib::RefPtr<Gdk::Texture>& t) {
    m_cache[page] = t;
    // Keep memory bounded on huge documents: thumbnails far from this one go.
    if (m_cache.size() > 200)
        for (auto it = m_cache.begin(); it != m_cache.end() && m_cache.size() > 150;)
            it = std::abs(it->first - page) > 60 ? m_cache.erase(it) : std::next(it);
}

}  // namespace lookit
