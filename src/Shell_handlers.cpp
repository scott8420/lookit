#include "Shell.hpp"
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "core/FileKind.hpp"
#include "core/Recents.hpp"
#include "core/TileText.hpp"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/convert.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>

#include <filesystem>
#include <memory>

// Shell_handlers.cpp -- HANDLERS. What a user action actually does.

namespace lookit {

// The one door every file comes through: the dialog, a recent, a drop, Files
// (App::on_open), the CLI, the tile. Open it, show it on View, remember it --
// or say in a toast why not and stay where we were.
void Shell::open_path(const std::string& path) {  // handler: open, show, remember
    // Unsaved page changes in the open PDF: don't drop them silently.
    if (m_dirty && (m_editing || m_pic_on) && path != m_doc_path) {
        auto dialog = Gtk::AlertDialog::create("Save the changes to " +
            (m_doc_path.empty() ? std::string("Untitled.pdf") : std::filesystem::path(m_doc_path).filename().string()) +
            " first?");
        dialog->set_buttons({"Cancel", "Don't Save", "Save"});
        dialog->set_cancel_button(0);
        dialog->set_default_button(2);
        dialog->choose(*this, [this, dialog, path](const Glib::RefPtr<Gio::AsyncResult>& res) {
            int pick = 0;
            try { pick = dialog->choose_finish(res); } catch (const Glib::Error&) { return; }
            if (pick == 0) return;
            if (pick == 2) {
                if (m_doc_path.empty()) { on_save_as(); return; }
                if (!save_to(m_doc_path)) return;
            }
            m_dirty = false;
            open_path(path);
        });
        return;
    }
    std::string error;
    auto doc = Document::open(path, error);
    if (!doc && error == Document::kLocked) {   // s011: ask, then open again
        ask_unlock(path, [this, path] { open_path(path); });
        return;
    }
    if (!doc) {
        if (auto lg = log::get(log::Area::Io)) lg->warn("open failed: {} -- {}", path, error);
        show_toast(std::filesystem::path(path).filename().string() + ": " + error, true);
        return;
    }
    std::shared_ptr<Document> shared = std::move(doc);
    m_view_empty.set_visible(false);
    m_doc_view.set_visible(true);
    go("view");
    m_doc_view.set_document(shared);
    m_thumbs.set_document(shared);
    if (shared->kind() == core::Kind::Pdf) begin_editing(path, shared->count());
    else {
        m_editing = false; m_dirty = false; m_thumbs.set_editable(false); m_doc_path.clear();
        if (shared->kind() == core::Kind::Image) begin_picture(shared);   // the picture tools (s010)
        else end_picture();
    }
    m_hits.clear();
    m_hit = -1;
    if (m_find_rev.get_reveal_child() && !m_find_text.empty()) start_search(m_find_text);
    note_recent(path);
    set_title(shared->name() + " -- Lookit");
    refresh_info();
    sync_view_bar();
    sync_edit_ui();
    present();
}

void Shell::on_open() {  // handler: the Open dialog
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Open");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto all = Gtk::FileFilter::create();
    all->set_name("PDFs and pictures");
    all->add_mime_type("application/pdf");
    all->add_pixbuf_formats();
    for (const char* p : {"*.pdf", "*.PDF", "*.heic", "*.avif", "*.webp", "*.jxl"}) all->add_pattern(p);
    filters->append(all);
    auto pdf = Gtk::FileFilter::create();
    pdf->set_name("PDF");
    pdf->add_mime_type("application/pdf");
    filters->append(pdf);
    auto pics = Gtk::FileFilter::create();
    pics->set_name("Pictures");
    pics->add_pixbuf_formats();
    filters->append(pics);
    auto any = Gtk::FileFilter::create();
    any->set_name("All files");
    any->add_pattern("*");
    filters->append(any);
    dialog->set_filters(filters);
    dialog->set_default_filter(all);
    if (auto d = m_doc_view.document())
        dialog->set_initial_folder(Gio::File::create_for_path(
            std::filesystem::path(d->path()).parent_path().string()));
    dialog->open(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (auto f = dialog->open_finish(res)) open_path(f->get_path());
        } catch (const Glib::Error& e) {
            if (auto lg = log::get(log::Area::Shell)) lg->info("open dialog: {}", e.what());   // cancelled
        }
    });
}

void Shell::on_recent_open(const std::string& p) {  // handler: a recent
    if (!std::filesystem::exists(p)) {
        core::recents_remove(m_recents, p);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents();
        show_toast("It's gone -- " + std::filesystem::path(p).filename().string() +
                   " was moved or deleted.", true);
        return;
    }
    open_path(p);
}

void Shell::on_recent_clear() {  // handler: empty the list
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents();
}

void Shell::on_page_changed() {  // handler: stack -> tint + nav row
    const std::string page = m_stack.get_visible_child_name();
    if (page.empty() || page == m_page) return;
    if (!m_page.empty()) remove_css_class("page-" + m_page);
    m_page = page;
    add_css_class("page-" + page);
    for (int i = 0; auto* row = m_nav.get_row_at_index(i); ++i)
        if (std::string(row->get_name()) == "nav." + page) { m_nav.select_row(*row); break; }
}

void Shell::on_page_entry() {  // handler: typed a page number
    try {
        const int n = std::stoi(std::string(m_page_entry.get_text()));
        m_doc_view.go_to(n - 1);
    } catch (...) {
    }
    sync_view_bar();
    gtk_widget_grab_focus(gtk_scrolled_window_get_child(m_doc_view.gobj()));
}

// ── Find ────────────────────────────────────────────────────────────────────
void Shell::on_find() {  // handler: Ctrl+F -- open the find bar
    if (!m_doc_view.document()) return;
    go("view");
    m_find_rev.set_reveal_child(true);
    m_find_entry.grab_focus();
    m_find_entry.select_region(0, -1);
}

void Shell::on_find_step(int dir) {  // handler: Enter / Shift+Enter / arrows
    if (m_hits.empty()) return;
    m_hit = core::search::step(m_hits, m_hit, dir);
    m_doc_view.show_hit(m_hit);
    sync_search_label();
}

void Shell::on_find_close() {  // handler: Esc / the X
    m_find_idle.disconnect();
    m_find_rev.set_reveal_child(false);
    m_find_text.clear();
    m_hits.clear();
    m_hit = -1;
    m_doc_view.set_hits({});
    sync_search_label();
    if (m_doc_view.document()) gtk_widget_grab_focus(gtk_scrolled_window_get_child(m_doc_view.gobj()));
}

void Shell::on_about() {  // handler: About (hide-on-close singleton)
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

void Shell::on_shortcuts() {  // handler: keyboard reference (renders from the registry)
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_toggle_left_drawer()  { if (m_drawers.toggle(core::Drawers::Side::Left))  apply_drawers(); }
void Shell::on_toggle_right_drawer() { if (m_drawers.toggle(core::Drawers::Side::Right)) apply_drawers(); }
void Shell::on_close_drawers()       { if (m_drawers.close()) apply_drawers(); }

// ── The quick-settings tile (Cairn's stone) ─────────────────────────────────
// Lookit's tile opens: the text is a path or a file:// link. Cleaned and capped
// by core::tile first (the same rules the tile's lib.js applies).
void Shell::receive_text(const std::string& raw) {  // handler: the tile / --send TEXT
    const auto req = core::tile::request(raw, core::tile::Where::Line);
    if (!req.error.empty()) { show_toast(req.error, true); present(); return; }
    if (req.text.empty()) return;
    std::string path = req.text;
    if (path.rfind("file://", 0) == 0) {
        try { path = Glib::filename_from_uri(path); } catch (const Glib::Error&) {}
    }
    if (path.size() > 1 && path.front() == '~') path = Glib::get_home_dir() + path.substr(1);
    if (path.size() > 1 && (path.front() == '\'' || path.front() == '"') && path.back() == path.front())
        path = path.substr(1, path.size() - 2);
    if (!std::filesystem::is_regular_file(path)) {
        show_toast("Not a file Lookit can find: " + req.text, true);
        present();
        return;
    }
    open_path(path);
}

void Shell::receive_clipboard() {  // handler: --send-clipboard / desktop action
    present();
    // On Wayland only the focused window may read the clipboard: wait until
    // active (or ~1.5 s, then try anyway).
    auto done = std::make_shared<bool>(false);
    auto conn = std::make_shared<sigc::connection>();
    auto read = [this, done, conn]() {
        if (*done) return;
        *done = true;
        conn->disconnect();
        get_clipboard()->read_text_async([this](Glib::RefPtr<Gio::AsyncResult>& res) {
            std::string text;
            try { text = get_clipboard()->read_text_finish(res); }
            catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell)) lg->warn("clipboard read failed: {}", e.what());
            }
            const auto req = core::tile::request(text, core::tile::Where::Clipboard);
            if (!req.error.empty()) show_toast(req.error, true);
            else receive_text(req.text);
        });
    };
    if (is_active()) { read(); return; }
    *conn = property_is_active().signal_changed().connect([this, read] { if (is_active()) read(); });
    Glib::signal_timeout().connect_once(read, 1500);
}

}  // namespace lookit
