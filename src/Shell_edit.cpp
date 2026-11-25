#include "Shell.hpp"
#include "Log.hpp"
#include "Save.hpp"
#include "core/FileKind.hpp"
#include "core/Recents.hpp"

#include <giomm/file.h>
#include <giomm/liststore.h>
#include <glibmm/main.h>
#include <gtkmm/alertdialog.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/menubutton.h>
#include <gtkmm/separator.h>

#include <filesystem>

// Shell_edit.cpp -- EDIT. The page tools: rotate, delete, duplicate, move
// (drag a thumbnail), insert / combine files (PDFs and pictures), extract,
// undo / redo, save. Every change goes through core::pages' verbs -- the ONE
// door (core/Undo.hpp) -- so each is one undo step; nothing here writes
// m_pages directly. The view is rebuilt from the page list after every journal
// change (apply_edit): composing is cheap, the PDFs are already open.

namespace lookit {

namespace fs = std::filesystem;
namespace P = core::pages;

void Shell::bind_edit() {  // edit: actions + keys
    m_pages.set_door([this] { return m_journal.open(); });
    m_journal.set_on_change([this] { apply_edit(); });

    // A picture has its own journal (s010); the page tools have theirs.
    m_act_undo = add_action("undo", [this] {
        if (m_pic_on ? m_pic_journal.undo().done : m_journal.undo().done) show_toast("Undone");
    });
    m_act_redo = add_action("redo", [this] { if (m_pic_on) m_pic_journal.redo(); else m_journal.redo(); });
    m_act_rotl = add_action("rotate-left",  [this] { edit_rotate(-1); });
    m_act_rotr = add_action("rotate-right", [this] { edit_rotate(+1); });
    m_act_delete = add_action("delete-page", [this] { edit_delete(); });
    m_act_dup = add_action("duplicate-page", [this] { edit_duplicate(); });
    m_act_insert = add_action("insert-file", sigc::mem_fun(*this, &Shell::on_insert_dialog));
    add_action("combine", sigc::mem_fun(*this, &Shell::on_combine_dialog));
    m_act_extract = add_action("extract", sigc::mem_fun(*this, &Shell::on_extract));
    m_act_save = add_action("save", sigc::mem_fun(*this, &Shell::on_save));
    add_action("save-as", sigc::mem_fun(*this, &Shell::on_save_as));

    // Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y and Delete, heard by the WINDOW in the
    // bubble phase -- only keys nothing else took -- and never taken from a
    // text box (the find and page entries keep their own). Cairn's undo stone.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
        auto* f = get_focus();
        if (f && (dynamic_cast<Gtk::Editable*>(f) || GTK_IS_TEXT(f->gobj()) || GTK_IS_TEXT_VIEW(f->gobj())))
            return false;
        const auto mods = st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::SHIFT_MASK |
                                Gdk::ModifierType::ALT_MASK);
        const auto C = Gdk::ModifierType::CONTROL_MASK, S = Gdk::ModifierType::SHIFT_MASK;
        if ((kv == GDK_KEY_z || kv == GDK_KEY_Z) && mods == C)           { activate_action("win.undo"); return true; }
        if ((kv == GDK_KEY_z || kv == GDK_KEY_Z) && mods == (C | S))     { activate_action("win.redo"); return true; }
        if ((kv == GDK_KEY_y || kv == GDK_KEY_Y) && mods == C)           { activate_action("win.redo"); return true; }
        if ((kv == GDK_KEY_Delete || kv == GDK_KEY_KP_Delete || kv == GDK_KEY_BackSpace) &&
            mods == Gdk::ModifierType{} && m_editing && m_stack.get_visible_child_name() == "view") {
            // A selected mark goes first; Delete with none takes the page
            // (Backspace never takes a page).
            if (delete_selected_mark()) return true;
            if (kv != GDK_KEY_BackSpace) { edit_delete(); return true; }
        }
        // A picture: Left / Right walk its folder (the view leaves them to us
        // when there's only the one page to show).
        if ((kv == GDK_KEY_Left || kv == GDK_KEY_Right) && mods == Gdk::ModifierType{} && m_pic_on &&
            m_stack.get_visible_child_name() == "view" && !m_doc_view.cropping()) {
            picture_browse(kv == GDK_KEY_Right ? +1 : -1);
            return true;
        }
        return false;
    }, false);
    add_controller(keys);

    // The Pages menu in the toolbar pill.
    auto pages = Gio::Menu::create();
    auto turn = Gio::Menu::create();
    turn->append("Rotate Left", "win.rotate-left");
    turn->append("Rotate Right", "win.rotate-right");
    turn->append("Duplicate Page", "win.duplicate-page");
    turn->append("Delete Page", "win.delete-page");
    pages->append_section(turn);
    auto files = Gio::Menu::create();
    files->append("Insert Pages from File…", "win.insert-file");
    files->append("Combine Files into One…", "win.combine");
    files->append("Extract Pages…", "win.extract");
    files->append("Password…", "win.password");   // s011 (Shell_forms.cpp)
    pages->append_section(files);
    auto sign = Gio::Menu::create();   // s008 (Shell_sign.cpp)
    sign->append("Add Signature", "win.sign");
    sign->append("Markup", "win.markup");
    sign->append("Signature…", "win.signature");
    sign->append("Export…", "win.export");
    pages->append_section(sign);
    m_edit_menu = Gio::Menu::create();
    pages->append_section(m_edit_menu);
    auto mb = Gtk::make_managed<Gtk::MenuButton>();
    mb->set_icon_name("document-edit-symbolic");
    mb->set_tooltip_text("Pages -- rotate, delete, insert, combine, extract, password, sign, export");
    mb->set_menu_model(pages);
    mb->add_css_class("flat");
    auto* info = m_bar.get_last_child();   // keep Info last
    m_bar.insert_child_after(*mb, *info->get_prev_sibling());

    // The extract bar: a pill under the find bar.
    m_extract_bar.add_css_class("view-bar");
    m_extract_bar.set_halign(Gtk::Align::CENTER);
    auto lbl = Gtk::make_managed<Gtk::Label>("Extract pages");
    lbl->add_css_class("doc-name");
    m_extract_entry.set_placeholder_text("e.g. 1-3, 7, 10-");
    m_extract_entry.set_width_chars(18);
    m_extract_entry.signal_activate().connect(sigc::mem_fun(*this, &Shell::on_extract_run));
    auto go_btn = Gtk::make_managed<Gtk::Button>("Save As…");
    go_btn->signal_clicked().connect(sigc::mem_fun(*this, &Shell::on_extract_run));
    auto close = Gtk::make_managed<Gtk::Button>();
    close->set_icon_name("window-close-symbolic");
    close->signal_clicked().connect([this] { m_extract_rev.set_reveal_child(false); });
    m_extract_msg.add_css_class("page-label");
    m_extract_bar.append(*lbl);
    m_extract_bar.append(m_extract_entry);
    m_extract_bar.append(*go_btn);
    m_extract_bar.append(m_extract_msg);
    m_extract_bar.append(*close);
    m_extract_rev.set_child(m_extract_bar);
    m_extract_rev.set_reveal_child(false);
    m_view.insert_child_after(m_extract_rev, m_find_rev);

    m_thumbs.signal_move().connect(sigc::mem_fun(*this, &Shell::edit_move));
    m_thumbs.signal_files().connect(sigc::mem_fun(*this, &Shell::insert_files));
    sync_edit_ui();
}

void Shell::begin_editing(const std::string& path, int pages) {  // edit: a PDF opened
    end_picture();
    m_pages.load(path, pages);
    m_journal.clear();          // fires apply_edit; m_editing still false -> no-op
    m_editing = true;
    m_dirty = false;
    m_doc_path = path;
    m_thumbs.set_editable(true);
    sync_edit_ui();
}

// The journal changed (a step, an undo, a redo): rebuild the document from the
// page list and show it where you were.
void Shell::apply_edit() {  // edit: journal changed -> view + thumbs
    if (!m_editing) { sync_edit_ui(); return; }
    std::string err;
    auto doc = Document::compose(m_pages.sources(), m_pages.records(),
                                 m_doc_path.empty() ? "Untitled.pdf" : m_doc_path, err, m_pages.doc().fields);
    const int page = m_doc_view.page();
    m_doc_view.refresh(doc, page);
    m_thumbs.set_document(doc);
    m_dirty = true;
    if (m_find_rev.get_reveal_child() && !m_find_text.empty()) start_search(m_find_text);
    refresh_info();
    sync_view_bar();
    sync_edit_ui();
}

void Shell::sync_edit_ui() {  // edit: Undo/Redo labels, title dot, greys
    if (!m_act_undo) return;
    // Whichever journal is live: the picture's or the pages'.
    const bool can_undo = m_pic_on ? m_pic_journal.can_undo() : m_journal.can_undo();
    const bool can_redo = m_pic_on ? m_pic_journal.can_redo() : m_journal.can_redo();
    const std::string undo_label = m_pic_on ? m_pic_journal.undo_label() : m_journal.undo_label();
    const std::string redo_label = m_pic_on ? m_pic_journal.redo_label() : m_journal.redo_label();
    m_act_undo->set_enabled(can_undo);
    m_act_redo->set_enabled(can_redo);
    m_edit_menu->remove_all();
    m_edit_menu->append(can_undo ? "Undo " + undo_label : "Undo", "win.undo");
    m_edit_menu->append(can_redo ? "Redo " + redo_label : "Redo", "win.redo");
    for (auto& a : {m_act_dup, m_act_extract}) a->set_enabled(m_editing);
    if (m_act_password) m_act_password->set_enabled(m_editing);
    for (auto& a : {m_act_rotl, m_act_rotr}) a->set_enabled(m_editing || m_pic_on);
    m_act_delete->set_enabled(m_editing && m_pages.count() > 1);
    m_act_insert->set_enabled(bool(m_doc_view.document()));
    m_act_save->set_enabled((m_editing && (m_dirty || m_doc_path.empty())) || (m_pic_on && m_dirty));
    if (const auto& d = m_doc_view.document()) {
        const std::string name = m_doc_path.empty() ? "Untitled.pdf" : fs::path(m_doc_path).filename().string();
        const bool live = m_editing || m_pic_on;
        set_title((m_dirty ? "• " : "") + (live ? name : d->name()) + " -- Lookit");
        if (live) m_doc_name.set_text((m_dirty ? "• " : "") + name);
    }
}

void Shell::edit_rotate(int q) {  // edit: the page you're on
    if (m_pic_on) { picture_rotate(q); return; }
    if (!m_editing) return;
    P::rotate(m_journal, m_pages, {m_pages.id_at(m_doc_view.page())}, q);
}

void Shell::edit_delete() {  // edit
    if (!m_editing) return;
    if (!P::remove(m_journal, m_pages, {m_pages.id_at(m_doc_view.page())}))
        show_toast("A document keeps at least one page.");
}

void Shell::edit_duplicate() {  // edit
    if (!m_editing) return;
    P::duplicate(m_journal, m_pages, m_pages.id_at(m_doc_view.page()));
}

void Shell::edit_move(int from, int to) {  // edit: a dragged thumbnail
    if (!m_editing) return;
    if (P::move(m_journal, m_pages, {m_pages.id_at(from)}, to))
        m_doc_view.go_to(to > from ? to - 1 : to);
}

// PDFs and pictures, inserted before position `at` (pictures become one-page
// PDFs first). With nothing editable open, the first file starts a new
// document -- an untitled one if it was a picture -- and the rest follow it:
// that is "combine".
void Shell::insert_files(const std::vector<std::string>& paths, int at) {  // edit: PDFs + pictures
    int added = 0;
    for (const auto& path : paths) {
        const auto kind = core::detect(path);
        std::string pdf = path, err;
        if (kind == core::Kind::Image) pdf = save::picture_to_pdf(path, err);
        else if (kind != core::Kind::Pdf) err = "not a PDF or a picture";
        std::unique_ptr<Document> probe;
        if (err.empty()) probe = Document::open(pdf, err);
        if (!probe && err == Document::kLocked) {   // s011: ask, then insert it where it was going
            ask_unlock(pdf, [this, path, at] { insert_files({path}, at); });
            continue;
        }
        if (!probe) {
            show_toast(fs::path(path).filename().string() + ": " + err, true);
            continue;
        }
        const int n = probe->count();
        if (!m_editing) {
            // Start from this one. A picture's PDF lives in the cache: untitled.
            begin_editing(pdf, n);
            m_doc_path = kind == core::Kind::Pdf ? path : "";
            std::shared_ptr<Document> shared = std::move(probe);
            m_view_empty.set_visible(false);
            m_doc_view.set_visible(true);
            go("view");
            m_doc_view.set_document(shared);
            m_thumbs.set_document(shared);
            m_dirty = m_doc_path.empty();
            at = n;
        } else {
            const int src = m_pages.add_source(pdf);
            P::insert(m_journal, m_pages, src, n, at, fs::path(path).filename().string());
            at += n;
        }
        ++added;
    }
    if (added) {
        refresh_info();
        sync_view_bar();
        sync_edit_ui();
        if (added > 1 || paths.size() > 1) show_toast("Added " + std::to_string(added) + " file(s).");
    }
}

namespace {
Glib::RefPtr<Gio::ListStore<Gtk::FileFilter>> pdf_and_picture_filters() {
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto all = Gtk::FileFilter::create();
    all->set_name("PDFs and pictures");
    all->add_mime_type("application/pdf");
    all->add_pixbuf_formats();
    filters->append(all);
    return filters;
}
}  // namespace

void Shell::on_insert_dialog() {  // edit: Insert Pages from File...
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Insert pages after this one");
    dialog->set_filters(pdf_and_picture_filters());
    dialog->open_multiple(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            std::vector<std::string> paths;
            for (const auto& f : dialog->open_multiple_finish(res)) paths.push_back(f->get_path());
            insert_files(paths, std::max(0, m_doc_view.page()) + 1);
        } catch (const Glib::Error&) {}
    });
}

void Shell::on_combine_dialog() {  // edit: Combine Files into One...
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Combine files into one PDF (in the order chosen)");
    dialog->set_filters(pdf_and_picture_filters());
    dialog->open_multiple(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        std::vector<std::string> paths;
        try {
            for (const auto& f : dialog->open_multiple_finish(res)) paths.push_back(f->get_path());
        } catch (const Glib::Error&) { return; }
        if (paths.empty()) return;
        // With an editable PDF open, they go on the end of it. Otherwise they
        // make a new, untitled document.
        if (!m_editing) {
            m_doc_view.set_document(nullptr);
        }
        insert_files(paths, m_editing ? m_pages.count() : 0);
        if (m_editing && !m_doc_path.empty() && paths.size() > 0 && m_doc_path == paths.front()) {}
    });
}

void Shell::on_extract() {  // edit: show the extract bar
    if (!m_editing) return;
    go("view");
    m_extract_entry.set_text(std::to_string(m_doc_view.page() + 1));
    m_extract_msg.set_text("of " + std::to_string(m_pages.count()));
    m_extract_rev.set_reveal_child(true);
    m_extract_entry.grab_focus();
}

void Shell::on_extract_run() {  // edit: ranges -> Save As a new PDF
    std::string err;
    const auto picks = P::parse_ranges(m_extract_entry.get_text(), m_pages.count(), err);
    if (!err.empty() || picks.empty()) {
        m_extract_msg.set_text(err.empty() ? "which pages?" : err);
        return;
    }
    std::vector<core::PageRec> recs;
    const auto all = m_pages.records();
    for (int i : picks) recs.push_back(all[std::size_t(i)]);
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Save the extracted pages as");
    const std::string base = m_doc_path.empty() ? "Untitled" : fs::path(m_doc_path).stem().string();
    dialog->set_initial_name(base + " (pages " + std::string(m_extract_entry.get_text()) + ").pdf");
    if (!m_doc_path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(fs::path(m_doc_path).parent_path().string()));
    dialog->save(*this, [this, dialog, recs](const Glib::RefPtr<Gio::AsyncResult>& res) {
        Glib::RefPtr<Gio::File> f;
        try { f = dialog->save_finish(res); } catch (const Glib::Error&) { return; }
        std::string e;
        if (write_doc(recs, f->get_path(), e)) {
            m_extract_rev.set_reveal_child(false);
            note_recent(f->get_path());
            show_toast("Extracted " + std::to_string(recs.size()) + " page(s) to " + f->get_basename());
        } else {
            show_toast("Extract failed: " + e, true);
        }
    });
}

void Shell::on_save() {  // edit: Ctrl+S
    if (m_pic_on) {   // over the file, if Lookit can write its kind; else Save As
        const auto e = fs::path(m_pic_path).extension().string();
        std::string x = e;
        for (auto& c : x) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (x == ".png" || x == ".jpg" || x == ".jpeg" || x == ".tif" || x == ".tiff" || x == ".bmp")
            picture_save_to(m_pic_path);
        else
            on_save_as();
        return;
    }
    if (!m_editing) return;
    if (m_doc_path.empty()) { on_save_as(); return; }
    save_to(m_doc_path);
}

void Shell::on_save_as() {  // edit: Ctrl+Shift+S
    if (!m_editing && !m_pic_on) return;
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title(m_pic_on ? "Save As -- .png, .jpg, .tif, .bmp or .pdf" : "Save As");
    std::string initial = m_doc_path.empty() ? "Untitled.pdf" : fs::path(m_doc_path).filename().string();
    if (m_pic_on) {   // a kind Lookit can't write (WebP, GIF, SVG...) is offered as PNG
        const auto x = fs::path(m_pic_path).extension().string();
        std::string l = x;
        for (auto& c : l) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (!(l == ".png" || l == ".jpg" || l == ".jpeg" || l == ".tif" || l == ".tiff" || l == ".bmp"))
            initial = fs::path(m_pic_path).stem().string() + ".png";
    }
    dialog->set_initial_name(initial);
    if (!m_doc_path.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(fs::path(m_doc_path).parent_path().string()));
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        try {
            if (auto f = dialog->save_finish(res)) save_to(f->get_path());
        } catch (const Glib::Error&) {}
    });
}

// Write the page list, then reopen what was written: the saved file is now the
// one clean source, the journal starts over, and you stay on your page.
bool Shell::save_to(const std::string& path) {  // edit: write, then reopen as clean
    if (m_pic_on) return picture_save_to(path);
    std::string err;
    if (!write_doc(m_pages.records(), path, err)) {
        show_toast("Couldn't save: " + err, true);
        return false;
    }
    const int page = std::max(0, m_doc_view.page());
    Document::forget(path);       // the pool's copy of the old file is stale now
    m_dirty = false;
    open_path(path);
    m_doc_view.go_to(page);
    show_toast("Saved " + fs::path(path).filename().string());
    return true;
}

// Unsaved changes: ask before the window goes. Save closes after saving
// (unless it's untitled -- then Save As, and you close again); Discard closes.
bool Shell::on_close_request() {  // edit: unsaved changes? ask
    if (!m_dirty || m_closing || !(m_editing || m_pic_on)) return Gtk::ApplicationWindow::on_close_request();
    auto dialog = Gtk::AlertDialog::create("Save the changes to " +
        (m_doc_path.empty() ? std::string("Untitled.pdf") : fs::path(m_doc_path).filename().string()) + "?");
    dialog->set_detail("Your changes are lost if you don't save them.");
    dialog->set_buttons({"Cancel", "Don't Save", "Save"});
    dialog->set_cancel_button(0);
    dialog->set_default_button(2);
    dialog->choose(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        int pick = 0;
        try { pick = dialog->choose_finish(res); } catch (const Glib::Error&) { return; }
        if (pick == 1) { m_closing = true; close(); }
        else if (pick == 2) {
            if (m_doc_path.empty()) { on_save_as(); return; }
            if (save_to(m_doc_path)) { m_closing = true; close(); }
        }
    });
    return true;   // not yet
}

}  // namespace lookit
