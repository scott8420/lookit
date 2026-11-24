#pragma once
#include "widgets/Widgets.hpp"
#include "core/Stones.hpp"   // s046: the undo stone's demo model + journal
#include "core/Drawers.hpp"  // the edge-drawer stone: which one is open

#include <gtkmm/applicationwindow.h>
#include <giomm/menu.h>
#include <giomm/simpleaction.h>

#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Shell -- the app window, AND the split-class exemplar.
//
// This class is split across multiple TUs by CATEGORY OF CODE, not by feature
// (CANON: "vocabulary first, file layout second" + "the header is the index").
// The split costs ~nothing while the parts are small; retrofitting it after a
// class grows to thousands of lines is a multi-session migration, so the
// structure is installed on day one. The header below IS the index -- every
// method carries a trailing `// category: ...` tag naming the file its body
// lives in. Read the tag, open that file.
//
// Vocabulary
// ----------
//   Zones    -- construction of named regions: the window frame, header,
//               sidebar+stack, the pattern pages, the menu model.
//               File: src/Shell_zones.cpp
//   Bindings -- what's wired to what: Gio actions, the recents action group,
//               accelerators.  File: src/Shell_bindings.cpp
//   Handlers -- slot bodies (on_*): what a user action actually does.
//               File: src/Shell_handlers.cpp
//   Helpers  -- reusable sync/orchestration called from zones/bindings/handlers.
//               File: src/Shell_helpers.cpp
//   Glue     -- what stays in Shell.cpp: the ctor + build_ui() (the ordered
//               construct -> name/register -> bind pass).
//
// Finding things
// --------------
//   "Where is the window built?"        -> Shell_zones.cpp    (build_shell)
//   "Where are actions registered?"     -> Shell_bindings.cpp (bind_actions)
//   "What does 'Dump registry' do?"     -> Shell_handlers.cpp (on_dump_registry)
//   "Where does the recents list sync?" -> Shell_helpers.cpp  (note_recent / rebuild_*)
//   "Where are the drawers built?"     -> Shell_zones.cpp    (build_drawers)
//   "What opens / closes a drawer?"    -> Shell_handlers.cpp (on_toggle_*_drawer)
//                                          + Shell_helpers.cpp (apply_drawers)
//   "Where does Ctrl+Z go?"             -> Shell_bindings.cpp (the window's undo keys)
//                                          + Shell_handlers.cpp (on_undo / on_redo)
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class AboutWindow;
class ShortcutsDialog;

class Shell : public Gtk::ApplicationWindow {
public:
    Shell();
    ~Shell() override;

    void build_ui();                             // category: glue

    // The quick-settings-tile stone: text from outside arrives here, through
    // App's two actions (app.receive-text / app.receive-clipboard).
    void receive_text(const std::string& raw);   // category: handler: the tile / --send TEXT
    void receive_clipboard();                    // category: handler: --send-clipboard / desktop action

private:
    // ── zones ──────────────────────────────────────────────────────────────
    void build_shell();                          // category: zone: window + sidebar + stack
    void build_pages();                          // category: zone: the pattern pages
    Glib::RefPtr<Gio::Menu> build_menu();        // category: zone: hamburger model
    void build_drawers();                        // category: zone: overlay + two edge drawers, tabs, scrim

    // ── bindings ───────────────────────────────────────────────────────────
    void bind_actions();                         // category: bindings: actions + recents group

    // ── handlers ───────────────────────────────────────────────────────────
    void on_pick_folder();                       // category: handler: folder chooser -> remember it
    void on_recent_open(const std::string& p);   // category: handler: open a recent (parameterised)
    void on_recent_clear();                      // category: handler: empty the recents list
    void on_dump_registry();                     // category: handler: print the live widget tree
    void on_about();                             // category: handler: open the About window (dialog-lifetime exemplar)
    void on_shortcuts();   // handler: open the keyboard reference (renders from core::shortcut_registry)
    // s046 -- the undo stone. Every verb on the Undo page goes through the
    // journal (core/Undo.hpp); nothing on it writes the model any other way.
    void on_undo();                              // category: handler: Ctrl+Z / menu / button
    void on_redo();                              // category: handler: Ctrl+Shift+Z, Ctrl+Y / menu / button
    void on_stone_add();                         // category: handler: a new stone below the selection
    void on_stone_delete();                      // category: handler: delete; the neighbour takes the selection
    void on_stone_up();                          // category: handler: swap with the one above
    void on_stone_down();                        // category: handler: swap with the one below
    void on_stone_rename();                      // category: handler: the name field, on Enter / leaving it
    // The edge-drawer stone. core::Drawers decides; apply_drawers paints.
    void on_toggle_left_drawer();                // category: handler: tab / F9 / menu
    void on_toggle_right_drawer();               // category: handler: tab / Shift+F9 / menu
    void on_close_drawers();                     // category: handler: scrim click / Esc
    void on_stone_size();                        // category: handler: the slider (keys merge; a drag is one step)

    // ── helpers ────────────────────────────────────────────────────────────
    void note_recent(const std::string& path);   // category: helper: push onto the list + persist
    void rebuild_recents_menu();                  // category: helper: repaint the submenu
    void rebuild_recents_page();                  // category: helper: repaint the live list page
    std::string recents_file() const;             // category: helper: the XDG path (UI-side resolution)
    void build_undo_page();                       // category: zone: the Undo page (s046)
    void build_tile_page();                       // category: zone: the Tile page (what the tile sent)
    void take_text(const std::string& raw, int where);   // category: helper: clean, cap, list, show
    void queue_stones_repaint();                  // category: helper: one repaint per idle, after the writes
    void repaint_stones();                        // category: helper: the list + the detail fields from the model
    void select_stone(const std::string& id, bool keys);   // category: helper: select (+ keyboard) after a repaint
    void update_undo_ui();                        // category: helper: Undo / Redo say what they would do, grey when nothing
    void apply_drawers();                         // category: helper: revealers + scrim from m_drawers
    void refresh_right_drawer();                  // category: helper: the report drawer refreshes on open
    bool focus_is_text() const;                   // category: helper: the keyboard is in a text box (its own Ctrl+Z)

    // Titlebar: brand logo (start) + hamburger (end).
    widgets::Button     m_logo_button;   // frameless brand mark; opens About
    widgets::MenuButton m_menu_button;

    // Body: sidebar | stack (each pattern is a stack page).
    widgets::Box          m_body;      // horizontal
    widgets::StackSidebar m_sidebar;
    widgets::Stack        m_stack;

    // The recents "live page": a label + a scrolling list rebuilt on change.
    widgets::Box            m_recents_page;   // vertical
    widgets::ScrolledWindow m_recents_scroll;
    widgets::ListBox        m_recents_list;

    // Recents state (core data; UI holds it + resolves the path -- the seam).
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;   // live submenu, rebuilt in place
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;  // held to toggle enabled state

    // s046 -- the undo stone. Declaration order matters: the store before the
    // journal that holds a reference to it.
    core::Stones       m_stones;
    core::StoneJournal m_journal{m_stones};
    std::string        m_sel;                  // the selected stone's id ("" = none)
    bool               m_painting = false;     // a repaint sets fields: not a user choice
    bool               m_repaint_queued = false;
    bool               m_keys_to_list = false; // after the repaint, give the list the keyboard
    widgets::Box       m_undo_page;
    widgets::ListBox   m_stone_list;
    widgets::Entry     m_stone_name;
    widgets::Scale     m_stone_size;
    widgets::Button    m_undo_button;
    widgets::Button    m_redo_button;
    widgets::Label     m_undo_said;            // "Undid: Delete “Jade”"
    Glib::RefPtr<Gio::Menu>         m_edit_menu;   // Undo / Redo items, re-labelled live
    Glib::RefPtr<Gio::SimpleAction> m_act_undo, m_act_redo;

    // The edge-drawer stone. The overlay's base is the stack (not the sidebar),
    // so the drawers slide over the content and never reflow it. Stacking order
    // (bottom -> top): scrim, tabs, revealers -- an open drawer covers its tab.
    core::Drawers       m_drawers;
    widgets::Overlay    m_overlay;
    widgets::Box        m_scrim;
    widgets::Button     m_left_tab, m_right_tab;
    widgets::Revealer   m_left_drawer, m_right_drawer;
    widgets::Box        m_left_panel, m_right_panel;     // the drawer contents
    widgets::Label      m_right_report;                  // refreshed each time it opens

    // The quick-settings-tile stone: the page that shows what arrived.
    widgets::Box        m_tile_page;
    widgets::Label      m_tile_said;    // the last arrival, or why it was refused
    widgets::ListBox    m_tile_list;    // newest first

    // The About window: a hide-on-close singleton, built lazily on first open.
    std::unique_ptr<AboutWindow>    m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;   // shortcut registry's GTK consumer (same lifetime stone)
};

}  // namespace lookit
