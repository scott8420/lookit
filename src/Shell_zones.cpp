#include "Shell.hpp"
#include "Log.hpp"

#include <gtkmm/gestureclick.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/togglebutton.h>

// Shell_zones.cpp -- ZONES. Construction of the named regions: the frame and
// header, the sidebar, the Home and View pages, the drawers, the menu model.

namespace lookit {

void Shell::build_shell() {  // zone: window, header, sidebar, overlay
    set_title("Lookit");
    set_default_size(1100, 760);

    auto header = Gtk::make_managed<Gtk::HeaderBar>();
    {
        auto* img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name("io.github.scott8420.Lookit-symbolic");
        img->set_pixel_size(22);
        m_logo_button.set_child(*img);
    }
    m_logo_button.set_has_frame(false);
    m_logo_button.set_tooltip_text("About Lookit");
    m_logo_button.set_action_name("win.about");
    header->pack_start(m_logo_button);

    auto* open = Gtk::make_managed<Gtk::Button>();
    open->set_icon_name("document-open-symbolic");
    open->set_tooltip_text("Open… (Ctrl+O)");
    open->set_action_name("win.open");
    header->pack_start(*open);

    m_menu_button.set_icon_name("open-menu-symbolic");
    m_menu_button.set_menu_model(build_menu());
    header->pack_end(m_menu_button);
    set_titlebar(*header);

    // The Rouster skin (Css.hpp): .lookit always, .page-<name> for the tint.
    // The stack is the one truth for "which page": rows ask it to move, and
    // on_page_changed follows it back to the row and the tint.
    add_css_class("lookit");
    build_sidebar();
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(160);
    m_stack.property_visible_child_name().signal_changed().connect(
        sigc::mem_fun(*this, &Shell::on_page_changed));

    build_drawers();   // the stack becomes the overlay's base

    // The toast: a message pill at the bottom, over everything but the drawers.
    m_toast_label.set_wrap(true);
    m_toast_label.set_max_width_chars(70);
    m_toast.set_child(m_toast_label);
    m_toast_label.add_css_class("toast");
    m_toast.set_transition_type(Gtk::RevealerTransitionType::SLIDE_UP);
    m_toast.set_halign(Gtk::Align::CENTER);
    m_toast.set_valign(Gtk::Align::END);
    m_toast.set_can_target(false);
    m_overlay.add_overlay(m_toast);

    m_body.append(m_side);
    m_body.append(m_overlay);
    set_child(m_body);
}

void Shell::build_sidebar() {  // zone: brand + nav rows
    m_side.add_css_class("sidebar-pane");
    m_side.set_size_request(180, -1);

    auto brand = Gtk::make_managed<Gtk::Label>("Lookit");
    brand->add_css_class("sidebar-brand");
    brand->set_xalign(0.0f);
    brand->set_margin_top(10);
    m_side.append(*brand);

    m_nav.set_selection_mode(Gtk::SelectionMode::SINGLE);
    struct Row { const char* name; const char* icon; const char* label; };
    for (const Row& r : {Row{"home", "lookit-home-symbolic", "Home"},
                         Row{"view", "lookit-view-symbolic", "View"}}) {
        auto row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(std::string("nav.") + r.name);
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
        auto img = Gtk::make_managed<Gtk::Image>();
        img->set_from_icon_name(r.icon);
        img->set_pixel_size(18);
        auto lbl = Gtk::make_managed<Gtk::Label>(r.label);
        lbl->set_xalign(0.0f);
        box->append(*img);
        box->append(*lbl);
        row->set_child(*box);
        m_nav.append(*row);
    }
    m_nav.signal_row_activated().connect([this](Gtk::ListBoxRow* row) {
        const std::string n = row->get_name();
        if (n.rfind("nav.", 0) == 0) go(n.substr(4));
    });
    m_nav.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string n = row->get_name();
        if (n.rfind("nav.", 0) == 0 && n.substr(4) != m_page) go(n.substr(4));
    });
    m_nav.set_vexpand(true);
    m_side.append(m_nav);

    auto foot = Gtk::make_managed<Gtk::Label>("F9 help · Shift+F9 info");
    foot->add_css_class("sidebar-foot");
    foot->set_xalign(0.0f);
    m_side.append(*foot);
}

// ─────────────────────────────────────────────────────────────────────────────
// Home -- the start page: the glassy logo, one line, the Open button, and the
// recent files as cards. The hero card is also the drop target's picture: the
// window takes the drop (bind_drop) and lights the card while a file hovers.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_home() {  // zone: the hero + recent cards
    m_home.set_margin(28);
    m_home.set_margin_top(18);

    m_hero.add_css_class("hero-card");
    m_hero.set_halign(Gtk::Align::CENTER);
    m_hero.set_size_request(560, -1);
    {
        auto logo = Gtk::make_managed<Gtk::Image>();
        logo->set_from_icon_name("io.github.scott8420.Lookit");
        logo->set_pixel_size(112);
        m_hero.append(*logo);
        auto title = Gtk::make_managed<Gtk::Label>("Lookit");
        title->add_css_class("hero-title");
        m_hero.append(*title);
        auto sub = Gtk::make_managed<Gtk::Label>("A quick look at PDFs and pictures.");
        sub->add_css_class("hero-sub");
        m_hero.append(*sub);
        auto open = Gtk::make_managed<Gtk::Button>("Open…");
        open->add_css_class("hero-button");
        open->set_halign(Gtk::Align::CENTER);
        open->set_margin_top(10);
        open->set_action_name("win.open");
        m_hero.append(*open);
        auto hint = Gtk::make_managed<Gtk::Label>("or drop a file anywhere on this window");
        hint->add_css_class("drop-hint");
        m_hero.append(*hint);
    }
    m_home.append(m_hero);

    m_recents_head.set_text("RECENT");
    m_recents_head.add_css_class("section-head");
    m_recents_head.set_xalign(0.0f);
    m_home.append(m_recents_head);

    m_recents_flow.add_css_class("recents");
    m_recents_flow.set_selection_mode(Gtk::SelectionMode::NONE);
    m_recents_flow.set_homogeneous(true);
    m_recents_flow.set_max_children_per_line(4);
    m_recents_flow.set_min_children_per_line(1);
    m_recents_flow.set_column_spacing(10);
    m_recents_flow.set_row_spacing(10);
    m_recents_flow.set_valign(Gtk::Align::START);
    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_child(m_recents_flow);
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    m_home.append(*scroll);

    m_stack.add(m_home, "home", "Home");
}

// ─────────────────────────────────────────────────────────────────────────────
// View -- a floating pill toolbar over the pages. Every control names a win.*
// action (bind_actions), so the menu, the keys and the buttons are one verb
// each. sync_view_bar paints the bar back from DocView's getters.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_view() {  // zone: the toolbar + DocView
    m_bar.add_css_class("view-bar");
    m_bar.set_halign(Gtk::Align::CENTER);

    auto icon_button = [](const char* icon, const char* tip, const char* action) {
        auto b = Gtk::make_managed<Gtk::Button>();
        b->set_icon_name(icon);
        b->set_tooltip_text(tip);
        b->set_action_name(action);
        return b;
    };
    m_doc_name.add_css_class("doc-name");
    m_doc_name.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
    m_doc_name.set_max_width_chars(36);
    m_bar.append(m_doc_name);
    m_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

    m_pages_group.append(*icon_button("go-up-symbolic", "Previous page", "win.prev-page"));
    m_page_entry.add_css_class("page-entry");
    m_page_entry.set_width_chars(4);
    m_page_entry.set_max_width_chars(5);
    m_page_entry.set_alignment(1.0f);
    m_page_entry.set_tooltip_text("Go to page (Ctrl+G)");
    m_page_entry.signal_activate().connect(sigc::mem_fun(*this, &Shell::on_page_entry));
    m_pages_group.append(m_page_entry);
    m_page_of.add_css_class("page-label");
    m_page_of.set_xalign(0.0f);
    m_pages_group.append(m_page_of);
    m_pages_group.append(*icon_button("go-down-symbolic", "Next page", "win.next-page"));
    m_pages_group.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    m_bar.append(m_pages_group);

    m_bar.append(*icon_button("zoom-out-symbolic", "Zoom out (Ctrl+−)", "win.zoom-out"));
    m_zoom_label.add_css_class("zoom-label");
    m_zoom_label.set_tooltip_text("Actual size (Ctrl+0)");
    {
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect([this](int, double, double) { activate_action("win.zoom-actual"); });
        m_zoom_label.add_controller(click);
    }
    m_bar.append(m_zoom_label);
    m_bar.append(*icon_button("zoom-in-symbolic", "Zoom in (Ctrl+=)", "win.zoom-in"));

    m_fit_width.set_icon_name("zoom-fit-best-symbolic");
    m_fit_width.set_tooltip_text("Fit width (Ctrl+1)");
    m_fit_width.signal_toggled().connect([this] {
        if (!m_syncing && m_fit_width.get_active()) activate_action("win.fit-width");
        else if (!m_syncing) sync_view_bar();
    });
    m_bar.append(m_fit_width);
    m_fit_page.set_icon_name("view-fullscreen-symbolic");
    m_fit_page.set_tooltip_text("Fit window (Ctrl+9)");
    m_fit_page.signal_toggled().connect([this] {
        if (!m_syncing && m_fit_page.get_active()) activate_action("win.fit-page");
        else if (!m_syncing) sync_view_bar();
    });
    m_bar.append(m_fit_page);
    // Book view: a stateful action, so this button and the menu's check mark
    // both follow its state with no syncing of ours.
    m_book.set_icon_name("lookit-book-symbolic");
    m_book.set_tooltip_text("Book view -- spreads, page 1 on the right (Ctrl+B)");
    m_book.set_action_name("win.book");
    m_bar.append(m_book);
    m_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    {
        auto thumbs = Gtk::make_managed<Gtk::ToggleButton>();
        thumbs->set_icon_name("view-paged-symbolic");
        thumbs->set_tooltip_text("Thumbnails (Ctrl+T)");
        thumbs->set_action_name("win.thumbs");
        m_bar.prepend(*thumbs);
    }
    m_bar.append(*icon_button("system-search-symbolic", "Find (Ctrl+F)", "win.find"));
    m_bar.append(*icon_button("dialog-information-symbolic", "Info (Shift+F9)", "win.toggle-right-drawer"));

    m_view.append(m_bar);

    // ── The find bar: a second pill under the first, revealed by Ctrl+F. ──
    // SearchEntry debounces typing itself (search-changed); Enter / Shift+Enter
    // step through the hits, Esc closes (stop-search).
    m_find_bar.add_css_class("view-bar");
    m_find_bar.add_css_class("find-bar");
    m_find_bar.set_halign(Gtk::Align::CENTER);
    m_find_entry.set_name("view.find_entry");
    m_find_entry.set_placeholder_text("Find in document");
    m_find_entry.set_size_request(260, -1);
    m_find_entry.signal_search_changed().connect([this] { start_search(m_find_entry.get_text()); });
    m_find_entry.signal_activate().connect([this] { on_find_step(+1); });
    m_find_entry.signal_next_match().connect([this] { on_find_step(+1); });
    m_find_entry.signal_previous_match().connect([this] { on_find_step(-1); });
    m_find_entry.signal_stop_search().connect([this] { on_find_close(); });
    {
        auto shift_enter = Gtk::EventControllerKey::create();
        shift_enter->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
            if ((kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) &&
                (st & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{}) {
                on_find_step(-1);
                return true;
            }
            return false;
        }, false);
        m_find_entry.add_controller(shift_enter);
    }
    m_find_bar.append(m_find_entry);
    m_find_count.add_css_class("page-label");
    m_find_count.set_width_chars(14);
    m_find_bar.append(m_find_count);
    m_find_bar.append(*icon_button("go-up-symbolic", "Previous match (Shift+Enter)", "win.find-prev"));
    m_find_bar.append(*icon_button("go-down-symbolic", "Next match (Enter)", "win.find-next"));
    m_find_bar.append(*icon_button("window-close-symbolic", "Close (Esc)", "win.find-close"));
    m_find_rev.set_child(m_find_bar);
    m_find_rev.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    m_find_rev.set_reveal_child(false);
    m_view.append(m_find_rev);

    // ── thumbs | page view ──
    m_thumbs.signal_pick().connect([this](int p) { m_doc_view.go_to(p); });
    m_thumbs.set_visible(false);
    m_view_body.append(m_thumbs);

    // Empty state: what View says with nothing open.
    m_view_empty.set_text("Nothing open. Ctrl+O, or drop a file here.");
    m_view_empty.add_css_class("view-empty");
    m_view_empty.set_vexpand(true);
    m_view_empty.set_hexpand(true);
    m_view_body.append(m_view_empty);
    m_view_body.append(m_doc_view);
    m_view_body.set_vexpand(true);
    m_view.append(m_view_body);
    m_doc_view.set_visible(false);
    m_doc_view.signal_changed().connect(sigc::mem_fun(*this, &Shell::sync_view_bar));

    m_stack.add(m_view, "view", "View");
}

// ─────────────────────────────────────────────────────────────────────────────
// The edge drawers (Cairn's stone). The stack is the overlay's BASE; the
// drawers float over it. Left = help, right = info about the open file.
// Stacking, bottom -> top: scrim, tabs, revealers.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::build_drawers() {  // zone: overlay + two edge drawers, tabs, scrim
    m_overlay.set_child(m_stack);
    m_overlay.set_hexpand(true);
    m_overlay.set_vexpand(true);

    m_left_panel.add_css_class("background");
    m_left_panel.add_css_class("drawer-panel");
    m_left_panel.add_css_class("left");
    m_left_panel.set_size_request(280, -1);
    {
        auto title = Gtk::make_managed<Gtk::Label>("Help");
        title->set_xalign(0.0f);
        title->add_css_class("title-4");
        auto body = Gtk::make_managed<Gtk::Label>();
        body->set_wrap(true);
        body->set_xalign(0.0f);
        body->set_yalign(0.0f);
        body->set_vexpand(true);
        body->set_markup(
            "<b>Open</b> with Ctrl+O, a drop, Files, or the quick-settings tile.\n\n"
            "<b>Read:</b> Space / Shift+Space page down / up, Home / End, "
            "← → a page at a time, Ctrl+G go to a page.\n\n"
            "<b>Find</b> (Ctrl+F): every match tinted; Enter / Shift+Enter step through "
            "them. <b>Thumbnails</b>: Ctrl+T.\n\n"
            "<b>Book view</b> (Ctrl+B): facing pages like a printed book -- "
            "page 1 alone on the right, then 2|3, 4|5 ...\n\n"
            "<b>Zoom:</b> Ctrl+= / Ctrl+−, Ctrl+0 actual size, Ctrl+1 fit width, "
            "Ctrl+9 fit window -- or Ctrl+wheel, or pinch.\n\n"
            "<b>Pages:</b> one page (or facing pair) at a time, fitted to the "
            "window; Space at the bottom turns the page. Continuous scrolling "
            "is in the View menu.\n\n"
            "<b>Info</b> about the file: Shift+F9.\n\n"
            "All keys: Ctrl+?");
        m_left_panel.append(*title);
        m_left_panel.append(*body);
    }
    m_left_drawer.set_child(m_left_panel);
    m_left_drawer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_RIGHT);
    m_left_drawer.set_transition_duration(240);
    m_left_drawer.set_halign(Gtk::Align::START);
    m_left_drawer.set_valign(Gtk::Align::FILL);
    m_left_drawer.set_reveal_child(false);

    m_right_panel.add_css_class("background");
    m_right_panel.add_css_class("drawer-panel");
    m_right_panel.add_css_class("right");
    m_right_panel.set_size_request(280, -1);
    {
        auto title = Gtk::make_managed<Gtk::Label>("Info");
        title->set_xalign(0.0f);
        title->add_css_class("title-4");
        m_info.set_wrap(true);
        m_info.set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        m_info.set_xalign(0.0f);
        m_info.set_yalign(0.0f);
        m_info.set_vexpand(true);
        m_info.set_selectable(true);
        m_right_panel.append(*title);
        m_right_panel.append(m_info);
    }
    m_right_drawer.set_child(m_right_panel);
    m_right_drawer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_LEFT);
    m_right_drawer.set_transition_duration(240);
    m_right_drawer.set_halign(Gtk::Align::END);
    m_right_drawer.set_valign(Gtk::Align::FILL);
    m_right_drawer.set_reveal_child(false);

    m_scrim.add_css_class("drawer-scrim");
    m_scrim.set_hexpand(true);
    m_scrim.set_vexpand(true);
    m_scrim.set_visible(false);
    {
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect([this](int, double, double) { on_close_drawers(); });
        m_scrim.add_controller(click);
    }

    m_left_tab.set_label("›");
    m_left_tab.add_css_class("drawer-tab");
    m_left_tab.set_halign(Gtk::Align::START);
    m_left_tab.set_valign(Gtk::Align::CENTER);
    m_left_tab.set_tooltip_text("Help (F9)");
    m_left_tab.set_action_name("win.toggle-left-drawer");
    m_right_tab.set_label("‹");
    m_right_tab.add_css_class("drawer-tab-right");
    m_right_tab.set_halign(Gtk::Align::END);
    m_right_tab.set_valign(Gtk::Align::CENTER);
    m_right_tab.set_tooltip_text("Info (Shift+F9)");
    m_right_tab.set_action_name("win.toggle-right-drawer");

    m_overlay.add_overlay(m_scrim);
    m_overlay.add_overlay(m_left_tab);
    m_overlay.add_overlay(m_right_tab);
    m_overlay.add_overlay(m_left_drawer);
    m_overlay.add_overlay(m_right_drawer);
}

Glib::RefPtr<Gio::Menu> Shell::build_menu() {  // zone: hamburger model
    auto menu = Gio::Menu::create();

    auto file = Gio::Menu::create();
    file->append("Open…", "win.open");
    file->append("Save", "win.save");
    file->append("Save As…", "win.save-as");
    file->append("Combine Files into One…", "win.combine");
    file->append("Export…", "win.export");
    m_recents_menu = Gio::Menu::create();
    file->append_submenu("Open Recent", m_recents_menu);
    menu->append_section(file);

    auto view = Gio::Menu::create();
    view->append("Zoom In", "win.zoom-in");
    view->append("Zoom Out", "win.zoom-out");
    view->append("Actual Size", "win.zoom-actual");
    view->append("Fit Width", "win.fit-width");
    view->append("Fit Window", "win.fit-page");
    view->append("Book View", "win.book");
    view->append("Thumbnails", "win.thumbs");
    view->append("Continuous Scrolling", "win.continuous");
    view->append("Find…", "win.find");
    view->append("Go to Page…", "win.go-to-page");
    menu->append_section(view);

    auto panels = Gio::Menu::create();
    panels->append("Help", "win.toggle-left-drawer");
    panels->append("Info", "win.toggle-right-drawer");
    menu->append_section(panels);

    auto app = Gio::Menu::create();
    app->append("Keyboard Shortcuts", "win.shortcuts");
    app->append("About Lookit", "win.about");
    app->append("Quit", "win.quit");
    menu->append_section(app);
    return menu;
}

}  // namespace lookit
