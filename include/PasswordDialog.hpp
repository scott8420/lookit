#pragma once
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/passwordentry.h>
#include <gtkmm/window.h>

#include <functional>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// PasswordDialog -- the two password questions (s011).
//
//   unlock:  "report.pdf is locked" -- one password; Open / Cancel. Shown
//            again, with "That password didn't open it.", until it opens or
//            the person cancels.
//   protect: Pages -> Password... -- a new password twice (they must match);
//            Protect / Cancel, and Remove Password when the PDF has one.
//            Nothing is written here: the Shell makes the answer one undo
//            step, and Save writes it.
//
// Hide-on-close singleton, like SignatureDialog (docs/resources-and-dialog-
// lifetime.md). A password is never logged and never kept past the run.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class PasswordDialog : public Gtk::Window {
public:
    explicit PasswordDialog(Gtk::Window& parent);

    enum class Answer { Cancel, Password, Remove };
    using Done = std::function<void(Answer, const std::string&)>;

    // `file`: the name to show. `again`: the last password was wrong.
    void ask_unlock(const std::string& file, bool again, Done done);
    // `locked`: the PDF has a password now (Remove Password is offered).
    void ask_protect(const std::string& file, bool locked, Done done);

private:
    void finish(Answer a);
    void on_primary();

    bool          m_protect = false;
    Done          m_done;
    Gtk::Box      m_box{Gtk::Orientation::VERTICAL, 10};
    Gtk::Label    m_title, m_detail, m_error;
    Gtk::PasswordEntry m_first, m_second;
    Gtk::Box      m_buttons{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button   m_cancel{"Cancel"}, m_remove{"Remove Password"}, m_ok;
};

}  // namespace lookit
