#include "Shell.hpp"
#include "Log.hpp"

#include <gdkmm/contentformats.h>
#include <giomm/file.h>
#include <giomm/simpleactiongroup.h>
#include <glibmm/variant.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/eventcontrollerkey.h>

// Shell_bindings.cpp -- BINDINGS. What's wired to what: the win.* actions (the
// menu, the toolbar and the keys all name these), the recents group, Esc for
// the drawers, and file drops. No slot bodies beyond a call -- those are
// handlers.

namespace lookit {

void Shell::bind_actions() {  // bindings: win.* + recents group
    add_action("open",      sigc::mem_fun(*this, &Shell::on_open));
    add_action("about",     sigc::mem_fun(*this, &Shell::on_about));
    add_action("shortcuts", sigc::mem_fun(*this, &Shell::on_shortcuts));
    add_action("quit",      [this]() { close(); });
    add_action("toggle-left-drawer",  sigc::mem_fun(*this, &Shell::on_toggle_left_drawer));
    m_act_info = add_action("toggle-right-drawer", sigc::mem_fun(*this, &Shell::on_toggle_right_drawer));

    // The view's verbs. Held so sync_view_bar can grey them with nothing open.
    m_act_zoom_in     = add_action("zoom-in",     [this] { m_doc_view.zoom_in(); });
    m_act_zoom_out    = add_action("zoom-out",    [this] { m_doc_view.zoom_out(); });
    m_act_zoom_actual = add_action("zoom-actual", [this] { m_doc_view.zoom_actual(); });
    m_act_fit_width   = add_action("fit-width",   [this] { m_doc_view.fit_width(); });
    m_act_fit_page    = add_action("fit-page",    [this] { m_doc_view.fit_page(); });
    m_act_book = add_action_bool("book", [this] {
        bool on = false;
        m_act_book->get_state(on);
        on = !on;
        m_act_book->change_state(on);
        m_doc_view.set_book(on);
    }, false);
    // Thumbnails: stateful (the toolbar toggle and the menu check follow it).
    // On by default; a one-page file never shows the strip (sync_view_bar).
    m_act_thumbs = add_action_bool("thumbs", [this] {
        bool on = false;
        m_act_thumbs->get_state(on);
        m_act_thumbs->change_state(!on);
        sync_view_bar();
    }, true);
    // Continuous scrolling: off by default -- View shows the page (or the
    // facing pair) fitted to the window, and turns pages (Scott, s005).
    m_act_continuous = add_action_bool("continuous", [this] {
        bool on = false;
        m_act_continuous->get_state(on);
        on = !on;
        m_act_continuous->change_state(on);
        m_doc_view.set_paged(!on);
    }, false);
    m_act_find = add_action("find", sigc::mem_fun(*this, &Shell::on_find));
    add_action("find-next",  [this] { on_find_step(+1); });
    add_action("find-prev",  [this] { on_find_step(-1); });
    add_action("find-close", sigc::mem_fun(*this, &Shell::on_find_close));
    add_action("next-page", [this] { m_doc_view.next_page(); });
    add_action("prev-page", [this] { m_doc_view.prev_page(); });
    add_action("go-to-page", [this] {
        if (!m_doc_view.document()) return;
        go("view");
        m_page_entry.grab_focus();
        m_page_entry.select_region(0, -1);
    });

    // Recents: one PARAMETERISED verb, recents.open(<path>), serves every row
    // of the submenu and every card (Cairn's recents stone).
    auto group = Gio::SimpleActionGroup::create();
    group->add_action_with_parameter(
        "open", Glib::Variant<Glib::ustring>::variant_type(),
        [this](const Glib::VariantBase& param) {
            on_recent_open(Glib::VariantBase::cast_dynamic<Glib::Variant<Glib::ustring>>(param).get());
        });
    m_recents_clear = Gio::SimpleAction::create("clear");
    m_recents_clear->signal_activate().connect([this](const Glib::VariantBase&) { on_recent_clear(); });
    group->add_action(m_recents_clear);
    insert_action_group("recents", group);
}

void Shell::bind_keys() {  // bindings: Esc for drawers
    // Esc closes an open drawer -- and ONLY when one is open, so it is never
    // taken from anything else otherwise. BUBBLE: only keys nothing else took.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType state) {
            const auto mods = state & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                       Gdk::ModifierType::ALT_MASK);
            if (keyval == GDK_KEY_Escape && mods == Gdk::ModifierType{} && m_drawers.scrim()) {
                on_close_drawers();
                return true;
            }
            return false;
        }, false);
    add_controller(keys);
}

// Files dropped anywhere on the window open; the first one is shown. While a
// file hovers, the window wears .dropping (Css.hpp lights the hero card).
void Shell::bind_drop() {  // bindings: files dropped on the window
    auto drop = Gtk::DropTarget::create(GDK_TYPE_FILE_LIST, Gdk::DragAction::COPY);
    drop->signal_enter().connect([this](double, double) {
        add_css_class("dropping");
        return Gdk::DragAction::COPY;
    }, false);
    drop->signal_leave().connect([this] { remove_css_class("dropping"); });
    drop->signal_drop().connect([this](const Glib::ValueBase& value, double, double) {
        remove_css_class("dropping");
        if (!G_VALUE_HOLDS(value.gobj(), GDK_TYPE_FILE_LIST)) return false;
        auto* list = static_cast<GdkFileList*>(g_value_get_boxed(value.gobj()));
        GSList* files = gdk_file_list_get_files(list);
        bool any = false;
        for (GSList* l = files; l; l = l->next) {
            if (char* p = g_file_get_path(G_FILE(l->data))) {
                if (!any) open_path(p);
                else note_recent(p);   // the rest wait on Home, a click away
                any = true;
                g_free(p);
            }
        }
        g_slist_free(files);
        if (auto lg = log::get(log::Area::Shell)) lg->info("drop: {}", any ? "opened" : "nothing usable");
        return any;
    }, false);
    add_controller(drop);
}

}  // namespace lookit
