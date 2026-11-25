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

namespace lookit {

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
            auto t = thumb(page);
            pic->set_paintable(t);
            // Reserve the page's shape so rows don't jump as thumbnails arrive.
            if (t) pic->set_size_request(kWidth, int(kWidth * double(t->get_height()) / t->get_width()));
        }
        if (num) num->set_text(std::to_string(page + 1));
    });
    m_list.set_factory(factory);
    m_list.set_model(m_sel);
    m_list.add_css_class("thumb-list");
    m_list.set_single_click_activate(false);
    set_child(m_list);

    m_sel->property_selected().signal_changed().connect([this] {
        if (m_syncing) return;
        const auto p = m_sel->get_selected();
        if (p != GTK_INVALID_LIST_POSITION) m_pick.emit(int(p));
    });
}

void ThumbStrip::set_document(std::shared_ptr<Document> doc) {
    m_doc = std::move(doc);
    m_cache.clear();
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
    m_cache[page] = t;
    // Keep memory bounded on huge documents: thumbnails far from this one go.
    if (m_cache.size() > 200)
        for (auto it = m_cache.begin(); it != m_cache.end() && m_cache.size() > 150;)
            it = std::abs(it->first - page) > 60 ? m_cache.erase(it) : std::next(it);
    return t;
}

}  // namespace lookit
