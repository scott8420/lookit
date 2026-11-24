#include "Shell.hpp"
#include "core/TileText.hpp"

#include <memory>
#include <glibmm/main.h>
#include "AboutWindow.hpp"
#include "ShortcutsDialog.hpp"
#include "Log.hpp"
#include "Registry.hpp"
#include "core/Recents.hpp"

#include <gtkmm/filedialog.h>
#include <giomm/asyncresult.h>
#include <giomm/file.h>

#include <filesystem>

// Shell_handlers.cpp -- HANDLERS. Slot bodies: what a user action actually does.
// Each is small and single-concern; orchestration it shares lives in helpers.

namespace lookit {

void Shell::on_pick_folder() {  // handler: folder chooser -> remember it
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Pick a folder to remember");
    dialog->select_folder(*this,
        [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
            try {
                auto dir = dialog->select_folder_finish(result);
                if (!dir) return;
                const std::string folder = dir->get_path();
                if (!folder.empty()) note_recent(folder);
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell))
                    lg->info("pick-folder: dialog dismissed ({})", e.what());
            }
        });
}

void Shell::on_recent_open(const std::string& path) {  // handler: open a recent
    if (path.empty()) return;
    // A real app would load the project here. The seed just proves the routing:
    // a vanished folder self-prunes; a live one is promoted back to the front.
    if (!std::filesystem::exists(path)) {
        core::recents_remove(m_recents, path);
        core::save_recents(m_recents_file, m_recents);
        rebuild_recents_menu();
        rebuild_recents_page();
        if (auto lg = log::get(log::Area::Recents))
            lg->warn("open '{}': gone -- pruned", path);
        return;
    }
    if (auto lg = log::get(log::Area::Recents)) lg->info("open '{}'", path);
    note_recent(path);   // move-to-front
}

void Shell::on_recent_clear() {  // handler: empty the recents list
    m_recents.clear();
    core::save_recents(m_recents_file, m_recents);
    rebuild_recents_menu();
    rebuild_recents_page();
}

void Shell::on_dump_registry() {  // handler: print the live widget tree
    // The debuggability payoff, on demand. Same call works from a gdb prompt.
    registry::dump();
}

void Shell::on_shortcuts() {  // handler: open the keyboard reference
    // Same lifetime stone as About: build once, re-present. The window's CONTENT
    // is not written here -- it renders by walking core::shortcut_registry(),
    // the same list bind_accelerators() wires from, so the two can't drift.
    if (!m_shortcuts) m_shortcuts = std::make_unique<ShortcutsDialog>();
    m_shortcuts->show(*this);
}

void Shell::on_about() {  // handler: open the About window (dialog-lifetime exemplar)
    // Lazily build once, then re-present. Hide-on-close singleton (CANON:
    // "Lifetime shape is design") -- the X hides it, so one instance lives for
    // the session and its named children never re-register.
    if (!m_about) m_about = std::make_unique<AboutWindow>(*this);
    m_about->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// s046 -- the undo stone's handlers. Each verb is ONE call into core::stones
// (which goes through the journal) and then says where the selection goes.
// None of them writes m_stones directly: the selftest's gate would not see a
// handler that did, so the rule is kept here by hand -- handlers call verbs.
// ─────────────────────────────────────────────────────────────────────────────
void Shell::on_undo() {  // handler: take the last step back
    const auto r = m_journal.undo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? std::string{} : r.selection.front();
    m_undo_said.set_text("Undid: " + r.label);
    select_stone(m_sel, true);
    if (auto lg = log::get(log::Area::Shell)) lg->info("undo: {}", r.label);
}

void Shell::on_redo() {  // handler: put the undone step back
    const auto r = m_journal.redo();
    if (!r.done) return;
    m_sel = r.selection.empty() ? std::string{} : r.selection.front();
    m_undo_said.set_text("Redid: " + r.label);
    select_stone(m_sel, true);
    if (auto lg = log::get(log::Area::Shell)) lg->info("redo: {}", r.label);
}

void Shell::on_stone_add() {  // handler: a new stone just below the selection
    const auto id = core::stones::add(m_journal, m_stones, "Stone", m_sel);
    m_journal.reselect({id});
    m_sel = id;
    select_stone(id, false);
    m_stone_name.grab_focus();   // name it next -- the field keeps its own Ctrl+Z
}

void Shell::on_stone_delete() {  // handler: delete; the neighbour takes the selection
    if (m_sel.empty()) return;
    const auto next = core::stones::neighbour(m_stones, m_sel);
    if (!core::stones::remove(m_journal, m_stones, m_sel)) return;
    m_sel = next;
    m_journal.reselect(next.empty() ? std::vector<std::string>{} : std::vector<std::string>{next});
    select_stone(next, true);   // the KEYBOARD goes with it: the next Ctrl+Z lands
}

void Shell::on_stone_up() {  // handler
    if (!m_sel.empty() && core::stones::move_up(m_journal, m_stones, m_sel)) select_stone(m_sel, true);
}

void Shell::on_stone_down() {  // handler
    if (!m_sel.empty() && core::stones::move_down(m_journal, m_stones, m_sel)) select_stone(m_sel, true);
}

void Shell::on_stone_rename() {  // handler: Enter in the name field, or leaving it
    if (m_painting || m_sel.empty()) return;
    const auto v = m_stones.get(m_sel);
    const std::string name = m_stone_name.get_text();
    if (v && v->name != name) core::stones::rename(m_journal, m_stones, m_sel, name);
}

void Shell::on_stone_size() {  // handler: the slider moved
    if (m_painting || m_sel.empty()) return;
    const int n = static_cast<int>(m_stone_size.get_value() + 0.5);
    // Mid-drag the gesture is open and this is just a write inside it; by
    // keys or the wheel it is a step that merges with the presses before it.
    if (m_journal.in_gesture()) core::stones::drag_to(m_journal, m_stones, m_sel, n);
    else                        core::stones::resize(m_journal, m_stones, m_sel, n);
}

// ── The edge drawers ────────────────────────────────────────────────────────
// The rule (one open at a time) is core::Drawers'; these only ask it and paint.
void Shell::on_toggle_left_drawer() {   // handler: tab / F9 / menu
    if (m_drawers.toggle(core::Drawers::Side::Left)) apply_drawers();
}

void Shell::on_toggle_right_drawer() {  // handler: tab / Shift+F9 / menu
    if (m_drawers.toggle(core::Drawers::Side::Right)) apply_drawers();
}

void Shell::on_close_drawers() {        // handler: scrim click / Esc
    if (m_drawers.close()) apply_drawers();
}

// ── The quick-settings-tile stone ──────────────────────────────────────────
// App's actions call these. Both end in take_text, which owns the rules.

void Shell::receive_text(const std::string& raw) {  // handler: the tile / --send TEXT
    take_text(raw, static_cast<int>(core::tile::Where::Line));
}

void Shell::receive_clipboard() {  // handler: --send-clipboard / desktop action
    m_stack.set_visible_child("tile");
    present();
    // On Wayland only the focused window may read the clipboard, so wait until
    // this window is active (or ~1.5 s, then try anyway). The tile itself never
    // needs this: the shell reads the clipboard and sends receive-text.
    auto done = std::make_shared<bool>(false);
    auto conn = std::make_shared<sigc::connection>();
    auto read = [this, done, conn]() {
        if (*done) return;
        *done = true;
        conn->disconnect();
        get_clipboard()->read_text_async([this](Glib::RefPtr<Gio::AsyncResult>& res) {
            std::string text;
            try {
                text = get_clipboard()->read_text_finish(res);
            } catch (const Glib::Error& e) {
                if (auto lg = log::get(log::Area::Shell)) lg->warn("tile: clipboard read failed: {}", e.what());
            }
            take_text(text, static_cast<int>(core::tile::Where::Clipboard));
        });
    };
    if (is_active()) { read(); return; }
    *conn = property_is_active().signal_changed().connect([this, read] { if (is_active()) read(); });
    Glib::signal_timeout().connect_once(read, 1500);
}

}  // namespace lookit
