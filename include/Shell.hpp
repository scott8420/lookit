#pragma once
#include "widgets/Widgets.hpp"
#include "core/Drawers.hpp"
#include "DocView.hpp"
#include "ThumbStrip.hpp"
#include "core/Pages.hpp"

#include <gtkmm/searchentry.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/popover.h>
#include <gtkmm/textview.h>
#include <gtkmm/togglebutton.h>

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
//   Edit     -- page tools, undo, insert / combine, save.  src/Shell_edit.cpp
//   Sign     -- signatures on pages; Export (convert).      src/Shell_sign.cpp
//   Markup   -- the markup bar; making / moving / editing marks.  src/Shell_markup.cpp
//   Glue     -- ctor + build_ui().                          src/Shell.cpp
//
// Finding things
//   "Where does a file get opened?"   -> Shell_handlers.cpp (open_path)
//   "Where are pages drawn?"          -> DocView.cpp (canvas_snapshot)
//   "Where is the toolbar repainted?" -> Shell_helpers.cpp (sync_view_bar)
//   "Where do drops land?"            -> Shell_bindings.cpp (bind_drop)
//   "Where does the tile's text go?"  -> Shell_handlers.cpp (receive_text)
//   "Where is a signature placed?"    -> Shell_sign.cpp (place_signature)
//   "Where are pages exported?"       -> Shell_sign.cpp (export_some)
//   "Where does a new highlight go?"  -> Shell_markup.cpp (on_mark_create)
//
// The page names ("home", "view") are the stack's names, the sidebar rows'
// "nav.<name>", and the window's ".page-<name>" CSS class (Css.hpp gradients).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class AboutWindow;
class ShortcutsDialog;
class SignatureDialog;

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
    void on_find();                              // category: handler: Ctrl+F -- open the find bar
    void on_find_step(int dir);                  // category: handler: Enter / Shift+Enter / arrows
    void on_find_close();                        // category: handler: Esc / the X
    void on_about();                             // category: handler: About
    void on_shortcuts();                         // category: handler: keyboard reference
    void on_toggle_left_drawer();                // category: handler: F9
    void on_toggle_right_drawer();               // category: handler: Shift+F9
    void on_close_drawers();                     // category: handler: scrim / Esc

    // ── edit (page tools) ────────────────────────────────────────────────────
    void bind_edit();                            // category: edit: actions + keys
    void begin_editing(const std::string& path, int pages);   // category: edit: a PDF opened
    void apply_edit();                           // category: edit: journal changed -> view + thumbs
    void sync_edit_ui();                         // category: edit: Undo/Redo labels, title dot, greys
    void edit_rotate(int quarter);               // category: edit: the page you're on
    void edit_delete();                          // category: edit
    void edit_duplicate();                       // category: edit
    void edit_move(int from, int to);            // category: edit: a dragged thumbnail
    void insert_files(const std::vector<std::string>& paths, int at);   // category: edit: PDFs + pictures
    void on_insert_dialog();                     // category: edit: Insert Pages from File...
    void on_combine_dialog();                    // category: edit: Combine Files into One...
    void on_extract();                           // category: edit: show the extract bar
    void on_extract_run();                       // category: edit: ranges -> Save As a new PDF
    void on_save();                              // category: edit: Ctrl+S
    void on_save_as();                           // category: edit: Ctrl+Shift+S
    bool save_to(const std::string& path);       // category: edit: write, then reopen as clean
    bool on_close_request() override;            // category: edit: unsaved changes? ask

    // ── sign + export (s008) ────────────────────────────────────────────────
    void bind_sign();                            // category: sign: actions, the export bar, view signals
    void on_sign();                              // category: sign: Add Signature (draw it first if none)
    void on_signature();                         // category: sign: Signature... -- draw / import / replace
    void place_signature(const std::string& art);   // category: sign: onto the page you're on
    void on_export();                            // category: sign: show the export bar
    void sync_export_bar();                      // category: sign: formats for this kind; dpi / pages greys
    void on_export_run();                        // category: sign: ranges + format -> where -> start
    void start_export(const std::string& target, bool folder);   // category: sign
    bool export_some();                          // category: sign: one page per idle slice; false = done

    // ── markup (s009) ───────────────────────────────────────────────────────
    void bind_markup();                          // category: markup: the bar, actions, view signals
    void on_markup();                            // category: markup: show / hide the bar
    void set_markup_tool(DocView::Tool t);       // category: markup: a tool button
    void set_markup_color(std::uint32_t c);      // category: markup: a swatch (recolours a selection)
    void sync_markup_bar();                      // category: markup: toggles follow the view's tool
    void on_mark_create(int page, core::Mark m); // category: markup: a new mark (notes / text: edit first)
    void on_mark_edit(int page, int index, core::Mark m, std::string verb);   // category: markup
    void on_mark_open(int page, int index);      // category: markup: double-click a note / text
    void edit_mark_text(int page, int index, core::Mark draft);   // category: markup: the text popover
    bool delete_selected_mark();                 // category: markup: Delete with a mark selected

    // ── helpers ────────────────────────────────────────────────────────────
    std::string recents_file() const;            // category: helper: the XDG path
    void note_recent(const std::string& path);   // category: helper: push + persist + repaint
    void rebuild_recents();                      // category: helper: the cards + the submenu
    void sync_view_bar();                        // category: helper: name, zoom, page, fit toggles
    void show_toast(const std::string& text, bool error = false);   // category: helper
    void apply_drawers();                        // category: helper: revealers + scrim
    void refresh_info();                         // category: helper: the Info drawer
    void go(const std::string& page);            // category: helper: show a page
    void start_search(const std::string& text);  // category: helper: (re)start, page by page on idle
    bool search_some();                          // category: helper: one idle slice; false = done
    void sync_search_label();                    // category: helper: "3 of 17", "searching..."

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
    widgets::Box          m_view_body;           // thumbs | the page view
    ThumbStrip            m_thumbs;
    widgets::Revealer     m_find_rev;
    widgets::Box          m_find_bar;
    Gtk::SearchEntry      m_find_entry;
    widgets::Label        m_find_count;
    std::vector<core::search::Hit> m_hits;
    int                   m_hit = -1;
    std::string           m_find_text;
    int                   m_find_page = 0;        // next page the idle search looks at
    sigc::connection      m_find_idle;
    DocView               m_doc_view;
    bool                  m_syncing = false;     // toolbar repaint: not a user choice

    // Recents (core data; the UI resolves the path -- the seam)
    std::vector<std::string>        m_recents;
    std::string                     m_recents_file;
    Glib::RefPtr<Gio::Menu>         m_recents_menu;
    Glib::RefPtr<Gio::SimpleAction> m_recents_clear;
    Glib::RefPtr<Gio::SimpleAction> m_act_zoom_in, m_act_zoom_out, m_act_zoom_actual,
                                    m_act_fit_width, m_act_fit_page, m_act_info, m_act_book,
                                    m_act_thumbs, m_act_find, m_act_continuous;

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

    // Page tools: the edited list, its journal, where it saves.
    core::Pages        m_pages;
    core::PageJournal  m_journal{m_pages};
    bool               m_editing = false;          // a PDF is open: the tools are live
    bool               m_dirty = false;
    bool               m_closing = false;          // the unsaved-changes answer was "close"
    std::string        m_doc_path;                 // "" = untitled (combined from pictures)
    Glib::RefPtr<Gio::SimpleAction> m_act_undo, m_act_redo, m_act_save, m_act_rotl, m_act_rotr,
                                    m_act_delete, m_act_dup, m_act_insert, m_act_extract;
    Glib::RefPtr<Gio::Menu>         m_edit_menu;   // Undo / Redo items, re-labelled live
    widgets::Revealer  m_extract_rev;
    widgets::Box       m_extract_bar;
    widgets::Entry     m_extract_entry;
    widgets::Label     m_extract_msg;

    // Sign + export
    Glib::RefPtr<Gio::SimpleAction> m_act_sign, m_act_export;
    widgets::Revealer  m_export_rev;
    widgets::Box       m_export_bar;
    Gtk::DropDown      m_export_format;
    widgets::Entry     m_export_pages;
    Gtk::DropDown      m_export_dpi;
    widgets::Label     m_export_msg;
    struct ExportJob {
        std::shared_ptr<Document> doc;
        std::vector<int> pages;
        int  format = 0;            // exporter::Format
        int  dpi = 150;
        std::string target;         // a file, or a folder for many pages
        bool folder = false;
        std::string stem;
        std::size_t next = 0;
        int  written = 0;
        std::string last;
    } m_job;
    sigc::connection   m_export_idle;
    std::unique_ptr<SignatureDialog> m_signature;

    // Markup bar
    Glib::RefPtr<Gio::SimpleAction> m_act_markup;
    widgets::Revealer  m_markup_rev;
    widgets::Box       m_markup_bar;
    std::vector<std::pair<DocView::Tool, Gtk::ToggleButton*>> m_tool_buttons;
    std::vector<std::pair<std::uint32_t, Gtk::ToggleButton*>> m_swatches;
    std::uint32_t      m_markup_color = 0;      // 0 = each tool's own default
    bool               m_markup_syncing = false;
    // The note / text editor: a popover over the page.
    std::unique_ptr<Gtk::Popover> m_text_pop;
    Gtk::TextView*     m_text_view = nullptr;
    Gtk::Button*       m_text_delete = nullptr;
    struct TextEdit { int page = -1, index = -1; core::Mark draft; bool open = false; } m_text_edit;
    void commit_text();                          // category: markup: the popover closed

    std::unique_ptr<AboutWindow>     m_about;
    std::unique_ptr<ShortcutsDialog> m_shortcuts;
};

}  // namespace lookit
