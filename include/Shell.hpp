#pragma once
#include "widgets/Widgets.hpp"
#include "core/Drawers.hpp"
#include "DocView.hpp"

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- Lookit's window. Split across TUs by CATEGORY (Cairn's discipline;
// the header is the index -- every method's `// category:` tag names its file).
//
//   Zones    -- the frame, sidebar, pages, drawers, menu.   src/Shell_zones.cpp
//   Bindings -- actions, keys, drag-and-drop.               src/Shell_bindings.cpp
//   Handlers -- what a user action does (on_*).             src/Shell_handlers.cpp
//   Helpers  -- reusable sync: recents, toolbar, toasts.    src/Shell_helpers.cpp
//   Glue     -- ctor + build_ui().                          src/Shell.cpp
//
// Finding things
//   "Where does a file get opened?"   -> Shell_handlers.cpp (open_path)
//   "Where are pages drawn?"          -> DocView.cpp (canvas_snapshot)
//   "Where is the toolbar repainted?" -> Shell_helpers.cpp (sync_view_bar)
//   "Where do drops land?"            -> Shell_bindings.cpp (bind_drop)
//   "Where does the tile's text go?"  -> Shell_handlers.cpp (receive_text)
//
// The page names ("home", "view") are the stack's names, the sidebar rows'
// "nav.<name>", and the window's ".page-<name>" CSS class (Css.hpp gradients).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class AboutWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

    // From outside the window (App): files to open, and the tile's text.
    void open_path(const std::string& path);     // category: handler: open, show, remember
    void receive_text(const std::string& raw);   // category: handler: the tile / --send TEXT
    void receive_clipboard();                    // category: handler: --send-clipboard

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window, header, sidebar, overlay
    void build_sidebar();                        // category: zone: brand + nav rows
    void build_home();                           // category: zone: the hero + recent cards
    void build_view();                           // category: zone: the toolbar + DocView
    void build_drawers();                        // category: zone: overlay + two edge drawers, tabs, scrim
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: win.* + recents group
    void bind_keys();                            // category: bindings: Esc for drawers
    void bind_drop();                            // category: bindings: files dropped on the window

    // ── handlers ───────────────────────────────────────────────────────────
    void on_open();                              // category: handler: the Open dialog
    void on_recent_open(const std::string& p);   // category: handler: a recent (parameterised)
    void on_recent_clear();                      // category: handler: empty the list
    void on_page_changed();                      // category: handler: stack -> tint + nav row
    void on_page_entry();                        // category: handler: typed a page number
    void on_about();                             // category: handler: About
    void on_shortcuts();                         // category: handler: keyboard reference
    void on_toggle_left_drawer();                // category: handler: F9
    void on_toggle_right_drawer();               // category: handler: Shift+F9
    void on_close_drawers();                     // category: handler: scrim / Esc

    // ── helpers ────────────────────────────────────────────────────────────
    std::string recents_file() const;            // category: helper: the XDG path
    void note_recent(const std::string& path);   // category: helper: push + persist + repaint
    void rebuild_recents();                      // category: helper: the cards + the submenu
    void sync_view_bar();                        // category: helper: name, zoom, page, fit toggles
    void show_toast(const std::string& text, bool error = false);   // category: helper
    void apply_drawers();                        // category: helper: revealers + scrim
    void refresh_info();                         // category: helper: the Info drawer
    void go(const std::string& page);            // category: helper: show a page

    // Titlebar
    widgets::Button     m_logo_button;
    widgets::MenuButton m_menu_button;

    // Body: our own sidebar | overlay(stack + drawers + toast)
    widgets::Box     m_body;
    widgets::Box     m_side;
    widgets::ListBox m_nav;
    widgets::Stack   m_stack;
    std::string      m_page;                     // the page whose tint is on

    // Home
    widgets::Box     m_home;
    widgets::Box     m_hero;
    widgets::FlowBox m_recents_flow;
    widgets::Label   m_recents_head;

    // View
    widgets::Box          m_view;
    widgets::Box          m_bar;
    widgets::Label        m_doc_name;
    widgets::Label        m_zoom_label;
    widgets::Entry        m_page_entry;
    widgets::Label        m_page_of;
    widgets::Box          m_pages_group;          // prev, page entry, of N, next
    widgets::ToggleButton m_fit_width;
    widgets::ToggleButton m_fit_page;
    widgets::ToggleButton m_book;                 // Book view (spreads); bound to win.book
    widgets::Label        m_view_empty;
    DocView               m_doc_view;
    bool                  m_syncing = false;     // toolbar repaint: not a user choice

    // Recents (core data; the UI resolves the path -- the seam)
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;
    Glib::RefPtr<Gio::SimpleAction> m_act_zoom_in, m_act_zoom_out, m_act_zoom_actual,
                                    m_act_fit_width, m_act_fit_page, m_act_info, m_act_book;

    // Drawers (left: help, right: info) + toast, over the stack
    core::Drawers     m_drawers;
    widgets::Overlay  m_overlay;
    widgets::Box      m_scrim;
    widgets::Button   m_left_tab, m_right_tab;
    widgets::Revealer m_left_drawer, m_right_drawer;
    widgets::Box      m_left_panel, m_right_panel;
    widgets::Label    m_info;
    widgets::Revealer m_toast;
    widgets::Label    m_toast_label;
    sigc::connection  m_toast_timer;

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace lookit
