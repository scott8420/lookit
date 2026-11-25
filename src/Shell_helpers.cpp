#include "Shell.hpp"
#include "core/Forms.hpp"

#include <set>
#include "Log.hpp"
#include "core/FileKind.hpp"
#include "core/Recents.hpp"

#include <giomm/menuitem.h>
#include <glibmm/main.h>
#include <glibmm/markup.h>
#include <glibmm/miscutils.h>
#include <glibmm/variant.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>

#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>

// Shell_helpers.cpp -- HELPERS. Reusable sync called from zones, bindings and
// handlers: recents (path, persist, two surfaces), the toolbar, toasts, the
// drawers and the Info panel.

namespace lookit {

namespace fs = std::filesystem;

std::string Shell::recents_file() const {  // helper: the XDG path
    return (fs::path(Glib::get_user_data_dir()) / "lookit" / "recent.json").string();
}

void Shell::note_recent(const std::string& path) {  // helper: push + persist + repaint
    if (path.empty()) return;
    core::recents_add(m_recents, path, 12);
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents();
}

// Two surfaces, one list (Cairn's recents stone): the cards on Home and the
// Open Recent submenu, both naming recents.open(<path>).
void Shell::rebuild_recents() {  // helper: the cards + the submenu
    while (auto* c = m_recents_flow.get_first_child()) m_recents_flow.remove(*c);
    m_recents_menu->remove_all();
    for (const auto& p : m_recents) {
        const fs::path fp(p);
        auto item = Gio::MenuItem::create(fp.filename().string(), "");
        item->set_action_and_target("recents.open", Glib::Variant<Glib::ustring>::create(p));
        m_recents_menu->append_item(item);

        auto card = Gtk::make_managed<Gtk::Button>();
        card->add_css_class("recent-card");
        card->set_tooltip_text(p);
        card->set_action_name("recents.open");
        card->set_action_target_value(Glib::Variant<Glib::ustring>::create(p));
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        auto kind = Gtk::make_managed<Gtk::Label>(core::kind_label(core::kind_from_name(p)));
        kind->add_css_class("recent-kind");
        kind->set_xalign(0.0f);
        auto name = Gtk::make_managed<Gtk::Label>(fp.filename().string());
        name->add_css_class("recent-name");
        name->set_xalign(0.0f);
        name->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        std::string where = fp.parent_path().string();
        const std::string home = Glib::get_home_dir();
        if (where.rfind(home, 0) == 0) where = "~" + where.substr(home.size());
        auto dir = Gtk::make_managed<Gtk::Label>(where);
        dir->add_css_class("recent-where");
        dir->set_xalign(0.0f);
        dir->set_ellipsize(Pango::EllipsizeMode::START);
        box->append(*kind);
        box->append(*name);
        box->append(*dir);
        card->set_child(*box);
        m_recents_flow.append(*card);
    }
    if (!m_recents.empty()) {
        auto clear = Gio::Menu::create();
        clear->append("Clear Recent", "recents.clear");
        m_recents_menu->append_section(clear);
    }
    m_recents_clear->set_enabled(!m_recents.empty());
    m_recents_head.set_visible(!m_recents.empty());
}

// The toolbar is painted FROM the view, never the other way: DocView's
// getters are the truth, and this runs on every signal_changed.
void Shell::sync_view_bar() {  // helper: name, zoom, page, fit toggles
    m_syncing = true;
    const auto& doc = m_doc_view.document();
    const bool has = bool(doc);
    m_doc_name.set_text(has ? doc->name() : "");
    {
        std::ostringstream z;
        z << int(std::lround(m_doc_view.scale() * 100)) << "%";
        m_zoom_label.set_text(has ? z.str() : "");
    }
    const int n = m_doc_view.count();
    if (!m_page_entry.has_focus())
        m_page_entry.set_text(has ? std::to_string(m_doc_view.page() + 1) : "");
    m_page_of.set_text(has ? "of " + std::to_string(n) : "");
    m_pages_group.set_visible(n > 1);   // a picture has no pages to step through
    bool thumbs_on = true;
    if (m_act_thumbs) m_act_thumbs->get_state(thumbs_on);
    m_thumbs.set_visible(has && n > 1 && thumbs_on);
    if (has) m_thumbs.set_current(m_doc_view.page());
    if (m_act_find) m_act_find->set_enabled(has && doc->kind() == core::Kind::Pdf);
    m_fit_width.set_active(has && m_doc_view.fit() == DocView::Fit::Width);
    m_fit_page.set_active(has && m_doc_view.fit() == DocView::Fit::Page);
    for (auto& a : {m_act_zoom_in, m_act_zoom_out, m_act_zoom_actual, m_act_fit_width, m_act_fit_page, m_act_book,
                    m_act_continuous})
        if (a) a->set_enabled(has);
    m_bar.set_sensitive(has);
    m_syncing = false;
}

void Shell::show_toast(const std::string& text, bool error) {  // helper
    m_toast_label.set_text(text);
    // Not "error": the theme styles that class (red text) behind our back.
    if (error) m_toast_label.add_css_class("toast-error");
    else m_toast_label.remove_css_class("toast-error");
    m_toast.set_reveal_child(true);
    m_toast_timer.disconnect();
    m_toast_timer = Glib::signal_timeout().connect_seconds(
        [this] { m_toast.set_reveal_child(false); return false; }, 5);
}

// ── Find: page by page on idle, so a long PDF never freezes the window ──────
// Hits appear (and are tinted) as their pages are searched; when the search
// reaches the end, the first hit on or after the page you're on becomes
// current -- unless you already stepped to one.
void Shell::start_search(const std::string& text) {  // helper: (re)start
    m_find_idle.disconnect();
    m_find_text = text;
    m_hits.clear();
    m_hit = -1;
    m_find_page = 0;
    m_doc_view.set_hits({});
    if (!text.empty() && m_doc_view.document())
        m_find_idle = Glib::signal_idle().connect(sigc::mem_fun(*this, &Shell::search_some));
    sync_search_label();
}

bool Shell::search_some() {  // helper: one idle slice; false = done
    const auto& doc = m_doc_view.document();
    if (!doc || m_find_text.empty()) return false;
    const int n = doc->count();
    const auto t0 = g_get_monotonic_time();
    bool grew = false;
    // ~12 ms of searching per slice, then let the window breathe.
    while (m_find_page < n && g_get_monotonic_time() - t0 < 12000) {
        auto found = doc->find(m_find_page++, m_find_text);
        grew = grew || !found.empty();
        m_hits.insert(m_hits.end(), found.begin(), found.end());
    }
    if (grew) m_doc_view.set_hits(m_hits);
    const bool done = m_find_page >= n;
    if (done && m_hit < 0 && !m_hits.empty()) {
        m_hit = core::search::first_from(m_hits, std::max(0, m_doc_view.page()));
        m_doc_view.show_hit(m_hit);
    }
    sync_search_label();
    if (done)
        if (auto lg = log::get(log::Area::Shell))
            lg->info("find \"{}\": {} hit(s) in {} page(s)", m_find_text, m_hits.size(), n);
    return !done;
}

void Shell::sync_search_label() {  // helper: "3 of 17", "searching..."
    const auto& doc = m_doc_view.document();
    std::string t;
    if (m_find_text.empty()) t = "";
    else if (doc && m_find_page < doc->count())
        t = std::to_string(m_hits.size()) + " so far…";
    else if (m_hits.empty())
        t = doc && doc->kind() != core::Kind::Pdf ? "no text here" : "no matches";
    else
        t = (m_hit >= 0 ? std::to_string(m_hit + 1) + " of " : "") + std::to_string(m_hits.size()) +
            (m_hit >= 0 ? "" : " matches");
    m_find_count.set_text(t);
}

void Shell::go(const std::string& page) {  // helper: show a page
    if (m_stack.get_child_by_name(page)) m_stack.set_visible_child(page);
}

void Shell::apply_drawers() {  // helper: revealers + scrim from m_drawers
    using Side = core::Drawers::Side;
    if (m_drawers.is_open(Side::Right)) refresh_info();
    m_left_drawer.set_reveal_child(m_drawers.is_open(Side::Left));
    m_right_drawer.set_reveal_child(m_drawers.is_open(Side::Right));
    m_scrim.set_visible(m_drawers.scrim());
    if (!m_drawers.scrim() && m_doc_view.document())
        gtk_widget_grab_focus(gtk_scrolled_window_get_child(m_doc_view.gobj()));
}

namespace {
std::string human_size(std::uintmax_t b) {
    const char* units[] = {"bytes", "KB", "MB", "GB"};
    double v = double(b);
    int u = 0;
    while (v >= 1024 && u < 3) { v /= 1024; ++u; }
    std::ostringstream o;
    if (u == 0) o << b << " bytes";
    else o << std::fixed << std::setprecision(v < 10 ? 1 : 0) << v << " " << units[u];
    return o.str();
}
std::string row(const std::string& k, const std::string& v) {
    return "<span size='small' alpha='60%'>" + Glib::Markup::escape_text(k) + "</span>\n<b>" +
           Glib::Markup::escape_text(v) + "</b>\n\n";
}
}  // namespace

void Shell::refresh_info() {  // helper: the Info drawer
    const auto& d = m_doc_view.document();
    if (!d) { m_info.set_markup("Nothing open."); return; }
    std::string m;
    m += row("Name", d->name());
    if (!d->title().empty() && d->title() != "untitled") m += row("Title", d->title());
    m += row("Kind", core::kind_label(d->kind()));
    std::error_code ec;
    const auto bytes = fs::file_size(d->path(), ec);
    if (!ec) m += row("Size", human_size(bytes));
    if (d->kind() == core::Kind::Image) {
        const auto s = d->size(0);
        m += row("Pixels", std::to_string(int(s.w)) + " × " + std::to_string(int(s.h)));
    } else {
        m += row("Pages", std::to_string(d->count()));
        if (d->count() > 0) {
            const auto s = d->size(std::max(0, m_doc_view.page()));
            std::ostringstream o;
            o << std::fixed << std::setprecision(2) << s.w / 72.0 << " × " << s.h / 72.0 << " in  ("
              << std::setprecision(0) << s.w * 25.4 / 72.0 << " × " << s.h * 25.4 / 72.0 << " mm)";
            m += row("This page", o.str());
        }
    }
    if (m_editing && d->kind() == core::Kind::Pdf) {   // s011
        std::set<std::string> names;   // a radio group's buttons are one field
        for (const auto& [pg, f] : d->all_fields())
            if (core::forms::fillable(f)) names.insert(std::to_string(f.src) + ":" + f.name);
        const int n = int(names.size());
        if (n) m += row("Form", std::to_string(n) + " field" + (n == 1 ? "" : "s") +
                                (core::forms::changed(m_pages) ? " (filled in, unsaved)" : ""));
        m += row("Password", locked_now() ? "yes" : "no");
    }
    m += row("Where", fs::path(d->path()).parent_path().string());
    m_info.set_markup(m);
}

}  // namespace lookit
