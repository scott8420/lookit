#include "Shell.hpp"
#include "Log.hpp"
#include "PasswordDialog.hpp"
#include "Save.hpp"
#include "core/Forms.hpp"

#include <glibmm/main.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/entry.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/textview.h>

#include <filesystem>

// Shell_forms.cpp -- FORMS (s011). Filling in a PDF's form, and locked PDFs.
//
// A click on a field (DocView::signal_field, Select tool, no mark there):
// a check box ticks / unticks, a radio is chosen, a text field or a choice
// opens a small popover over it. Every value goes through core::forms::set --
// the ONE door, one step in the page tools' journal (the document record,
// Pages::kDoc) -- and the view redraws from the record (apply_edit -> compose
// pushes the values into poppler, which draws the field's look).
//
// A locked PDF: Document::open answers kLocked; we ask (PasswordDialog), keep
// the password for this run (Document::remember_password) and open again. The
// lock Save writes is the record's too: Pages -> Password... adds, changes or
// removes it, as one undo step.

namespace lookit {

namespace fs = std::filesystem;
namespace F = core::forms;

void Shell::bind_forms() {  // forms: view signal, Password..., the field popover
    m_doc_view.signal_field().connect(sigc::mem_fun(*this, &Shell::on_field));
    m_act_password = add_action("password", sigc::mem_fun(*this, &Shell::on_password));
    m_act_password->set_enabled(false);

    m_field_pop = std::make_unique<Gtk::Popover>();
    m_field_pop->set_parent(m_doc_view);
    m_field_pop->set_autohide(true);
    m_field_pop->signal_closed().connect(sigc::mem_fun(*this, &Shell::commit_field));
    // Tab / Shift+Tab: keep what's typed, go on to the next field. Esc: leave
    // it as it was. Enter (one line) / Ctrl+Enter (several): keep it.
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
        const bool ctrl = (st & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
        if (kv == GDK_KEY_Escape) { m_fedit.skip = true; m_field_pop->popdown(); return true; }
        if (kv == GDK_KEY_Tab || kv == GDK_KEY_ISO_Left_Tab) {
            m_fedit.step = kv == GDK_KEY_ISO_Left_Tab ? -1 : +1;
            m_field_pop->popdown();
            return true;
        }
        if ((kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) && (ctrl || !m_field_view)) {
            m_field_pop->popdown();
            return true;
        }
        return false;
    }, false);
    keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);   // before the entry takes Enter / Tab
    m_field_pop->add_controller(keys);
}

std::vector<core::Field> Shell::field_group(const core::Field& f) const {  // forms: same name, same source
    std::vector<core::Field> out;
    if (const auto& d = m_doc_view.document())
        for (auto& [pg, g] : d->all_fields())
            if (g.src == f.src && g.name == f.name && g.kind == f.kind) out.push_back(g);
    return out;
}

void Shell::on_field(int page, core::Field f) {  // forms: a click on a field: tick / choose / type
    if (!m_editing) return;
    if (f.read_only) { show_toast("“" + f.name + "” can't be changed -- the form makes it read-only."); return; }
    switch (f.kind) {
        case core::FieldKind::Check:
            F::set(m_journal, m_pages, f, field_group(f), F::on(!F::is_on(f.value)));
            return;
        case core::FieldKind::Radio:
            F::set(m_journal, m_pages, f, field_group(f), F::on(true));
            return;
        case core::FieldKind::Text:
        case core::FieldKind::Combo:
        case core::FieldKind::List:
            edit_field(page, f);
            return;
        case core::FieldKind::Signature:
            show_toast("A signature field -- sign over it with Add Signature (Pages menu).");
            return;
        default:
            show_toast("Lookit doesn't run a form's buttons.");
            return;
    }
}

// The popover: built fresh for each field (a text box, a list of choices, the
// boxes of a list that takes several). It closes on Enter, Tab, Esc or a
// click elsewhere; commit_field decides what that meant.
void Shell::edit_field(int page, const core::Field& f) {  // forms: the popover for text and choices
    if (page != m_doc_view.page()) m_doc_view.go_to(page);
    m_fedit = {page, f, true, false, 0};
    m_field_entry = nullptr;
    m_field_view = nullptr;
    m_field_checks.clear();

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(6);
    auto* head = Gtk::make_managed<Gtk::Label>(f.name);
    head->set_xalign(0);
    head->add_css_class("dim-label");
    box->append(*head);
    Gtk::Widget* focus = nullptr;

    if (f.kind == core::FieldKind::Text) {
        if (f.multiline) {
            auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
            sw->set_size_request(std::max(260, int(f.box.w * m_doc_view.scale())), 110);
            m_field_view = Gtk::make_managed<Gtk::TextView>();
            m_field_view->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
            m_field_view->get_buffer()->set_text(f.value);
            sw->set_child(*m_field_view);
            box->append(*sw);
            focus = m_field_view;
        } else {
            m_field_entry = Gtk::make_managed<Gtk::Entry>();
            m_field_entry->set_text(f.value);
            m_field_entry->set_width_chars(std::clamp(int(f.box.w / 7), 16, 48));
            if (f.max_len > 0) m_field_entry->set_max_length(f.max_len);
            if (f.password) m_field_entry->set_visibility(false);
            box->append(*m_field_entry);
            focus = m_field_entry;
        }
        auto* hint = Gtk::make_managed<Gtk::Label>(f.multiline ? "Ctrl+Enter keeps it · Tab: next field · Esc: leave it"
                                                               : "Enter keeps it · Tab: next field · Esc: leave it");
        hint->set_xalign(0);
        hint->add_css_class("caption");
        hint->add_css_class("dim-label");
        box->append(*hint);
    } else if (f.multi) {   // a list that takes several: boxes, kept on close
        const auto picks = F::picked(f.value);
        for (int i = 0; i < int(f.options.size()); ++i) {
            auto* c = Gtk::make_managed<Gtk::CheckButton>(f.options[std::size_t(i)]);
            c->set_active(std::find(picks.begin(), picks.end(), i) != picks.end());
            m_field_checks.push_back(c);
            box->append(*c);
        }
        if (!m_field_checks.empty()) focus = m_field_checks.front();
    } else {   // one pick: a click chooses and closes
        const auto picks = F::picked(f.value);
        for (int i = 0; i < int(f.options.size()); ++i) {
            auto* b = Gtk::make_managed<Gtk::Button>(f.options[std::size_t(i)]);
            b->add_css_class("flat");
            if (auto* l = dynamic_cast<Gtk::Label*>(b->get_child())) l->set_xalign(0);
            if (std::find(picks.begin(), picks.end(), i) != picks.end()) b->add_css_class("suggested-action");
            b->signal_clicked().connect([this, i] {
                m_fedit.skip = true;
                F::set(m_journal, m_pages, m_fedit.f, field_group(m_fedit.f), F::picks({i}));
                m_field_pop->popdown();
            });
            box->append(*b);
            if (!focus) focus = b;
        }
        if (f.editable) {   // a combo you can type into: your own words
            box->append(*Gtk::make_managed<Gtk::Separator>());
            m_field_entry = Gtk::make_managed<Gtk::Entry>();
            m_field_entry->set_placeholder_text("Or type your own");
            if (F::is_typed(f.value)) m_field_entry->set_text(F::typed_words(f.value));
            box->append(*m_field_entry);
        }
    }
    m_field_pop->set_child(*box);
    const auto r = m_doc_view.field_screen_box(page, f);
    m_field_pop->set_pointing_to(Gdk::Rectangle(int(r.x), int(r.y), std::max(1, int(r.w)), std::max(1, int(r.h))));
    m_field_pop->set_position(Gtk::PositionType::BOTTOM);
    m_doc_view.set_field_focus(page, f.id);
    m_field_pop->popup();
    if (focus) focus->grab_focus();
    if (m_field_entry && f.kind == core::FieldKind::Text) m_field_entry->select_region(0, -1);
}

void Shell::commit_field() {  // forms: the popover closed
    if (!m_fedit.open) return;
    m_fedit.open = false;
    m_doc_view.set_field_focus(-1, -1);
    const auto f = m_fedit.f;
    const int step = m_fedit.step;
    if (!m_fedit.skip && m_editing) {
        std::string v = f.value;
        if (f.kind == core::FieldKind::Text) {
            v = m_field_view ? std::string(m_field_view->get_buffer()->get_text()) : std::string(m_field_entry->get_text());
        } else if (f.multi) {
            std::vector<int> picks;
            for (int i = 0; i < int(m_field_checks.size()); ++i)
                if (m_field_checks[std::size_t(i)]->get_active()) picks.push_back(i);
            v = F::picks(picks);
        } else if (m_field_entry && !std::string(m_field_entry->get_text()).empty() &&
                   std::string(m_field_entry->get_text()) != F::typed_words(f.value)) {
            v = F::typed(m_field_entry->get_text());
        }
        F::set(m_journal, m_pages, f, field_group(f), v);
    }
    m_fedit.skip = false;
    m_fedit.step = 0;
    if (step) {   // after the popover has gone: open the next one
        Glib::signal_idle().connect_once([this, step] { field_step(step); });
    } else {
        m_doc_view.grab_focus();
    }
}

void Shell::field_step(int dir) {  // forms: Tab / Shift+Tab to the next field
    const auto& d = m_doc_view.document();
    if (!d || !m_editing) return;
    std::vector<std::pair<int, core::Field>> all;
    for (auto& pf : d->all_fields()) {
        const auto k = pf.second.kind;
        if (F::fillable(pf.second) && (k == core::FieldKind::Text || k == core::FieldKind::Combo || k == core::FieldKind::List))
            all.push_back(std::move(pf));
    }
    if (all.empty()) return;
    const auto order = F::tab_order(all);
    int at = -1;
    for (int i = 0; i < int(order.size()); ++i) {
        const auto& [pg, f] = all[std::size_t(order[std::size_t(i)])];
        if (pg == m_fedit.page && f.id == m_fedit.f.id && f.src == m_fedit.f.src) { at = i; break; }
    }
    const int n = int(order.size());
    const int next = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
    const auto& [pg, f] = all[std::size_t(order[std::size_t(next)])];
    edit_field(pg, f);
}

// ── locked PDFs ─────────────────────────────────────────────────────────────

void Shell::ask_unlock(const std::string& path, std::function<void()> then) {  // forms: a locked PDF
    // On idle: at startup the window is presented after open_path returns, and
    // the question must go over it, not under.
    Glib::signal_idle().connect_once([this, path, then] {
        present();
        if (!m_password) m_password = std::make_unique<PasswordDialog>(*this);
        const bool again = !Document::password(path).empty();   // one was tried and didn't open it
        m_password->ask_unlock(fs::path(path).filename().string(), again,
                               [this, path, then](PasswordDialog::Answer a, const std::string& pw) {
            if (a != PasswordDialog::Answer::Password) {
                Document::remember_password(path, "");
                show_toast(fs::path(path).filename().string() + " stays locked.");
                return;
            }
            Document::remember_password(path, pw);
            Document::forget(path);
            then();   // opens again; a wrong password comes back here
        });
    });
}

bool Shell::locked_now() const {  // forms: will Save write a password?
    const auto& l = m_pages.doc().lock;
    if (l.mode == core::Lock::Mode::Password) return true;
    if (l.mode == core::Lock::Mode::None || m_pages.sources().empty()) return false;
    return !Document::password(m_pages.sources().front()).empty();
}

void Shell::on_password() {  // forms: Pages -> Password...
    if (!m_editing) return;
    if (!m_password) m_password = std::make_unique<PasswordDialog>(*this);
    const std::string name = m_doc_path.empty() ? "Untitled.pdf" : fs::path(m_doc_path).filename().string();
    m_password->ask_protect(name, locked_now(), [this](PasswordDialog::Answer a, const std::string& pw) {
        using M = core::Lock::Mode;
        if (a == PasswordDialog::Answer::Cancel) return;
        // Removing the file's own lock, or putting it back: Keep when that is
        // what the file already is, so an undo-free round trip isn't a change.
        core::Lock want = a == PasswordDialog::Answer::Remove ? core::Lock{M::None, ""} : core::Lock{M::Password, pw};
        const bool file_locked = !m_pages.sources().empty() && !Document::password(m_pages.sources().front()).empty();
        if (want.mode == M::None && !file_locked) want = {};
        if (want.mode == M::Password && file_locked && pw == Document::password(m_pages.sources().front())) want = {};
        if (!core::pages::set_lock(m_journal, m_pages, want)) return;
        show_toast(want.mode == M::None ? "The saved PDF will open without a password -- Ctrl+S to save"
                                        : "The saved PDF will ask for this password -- Ctrl+S to save");
    });
}

// Save and Extract: the form values filled in (poppler writes a copy of each
// filled source), the lock from the record, the temp copies gone after.
bool Shell::write_doc(const std::vector<core::PageRec>& recs, const std::string& path, std::string& err) {  // forms
    std::vector<std::string> temps;
    auto sources = Document::filled_sources(m_pages.sources(), m_pages.doc().fields, temps, err);
    const bool ok = !sources.empty() && save::write_pdf(sources, recs, path, err, m_pages.doc().lock);
    for (const auto& t : temps) {
        std::error_code ec;
        fs::remove(t, ec);
        Document::remember_password(t, "");
    }
    if (ok) {   // what will open it now
        const auto& l = m_pages.doc().lock;
        Document::remember_password(path, l.mode == core::Lock::Mode::Password ? l.password
                                         : l.mode == core::Lock::Mode::None   ? std::string{}
                                         : Document::password(m_pages.sources().front()));
    }
    return ok;
}

}  // namespace lookit
