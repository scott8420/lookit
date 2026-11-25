#include "PasswordDialog.hpp"

#include <gtkmm/eventcontrollerkey.h>

namespace lookit {

PasswordDialog::PasswordDialog(Gtk::Window& parent) {
    set_transient_for(parent);
    set_modal(true);
    set_hide_on_close(true);
    set_resizable(false);
    set_default_size(380, -1);
    add_css_class("password-dialog");

    m_box.set_margin(18);
    m_title.add_css_class("title-3");
    m_title.set_xalign(0);
    m_title.set_wrap(true);
    m_detail.set_xalign(0);
    m_detail.set_wrap(true);
    m_detail.add_css_class("dim-label");
    m_error.set_xalign(0);
    m_error.add_css_class("error");
    for (auto* e : {&m_first, &m_second}) {
        e->set_show_peek_icon(true);
        e->property_activates_default() = true;
    }
    m_buttons.set_halign(Gtk::Align::END);
    m_buttons.set_margin_top(6);
    m_ok.add_css_class("suggested-action");
    m_remove.add_css_class("destructive-action");
    m_buttons.append(m_cancel);
    m_buttons.append(m_remove);
    m_buttons.append(m_ok);
    for (auto* w : std::initializer_list<Gtk::Widget*>{&m_title, &m_detail, &m_first, &m_second, &m_error, &m_buttons})
        m_box.append(*w);
    set_child(m_box);
    set_default_widget(m_ok);

    m_cancel.signal_clicked().connect([this] { finish(Answer::Cancel); });
    m_remove.signal_clicked().connect([this] { finish(Answer::Remove); });
    m_ok.signal_clicked().connect(sigc::mem_fun(*this, &PasswordDialog::on_primary));
    signal_close_request().connect([this] { finish(Answer::Cancel); return true; }, false);
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) {
        if (kv == GDK_KEY_Escape) { finish(Answer::Cancel); return true; }
        return false;
    }, false);
    add_controller(keys);
}

void PasswordDialog::ask_unlock(const std::string& file, bool again, Done done) {
    m_protect = false;
    m_done = std::move(done);
    set_title("Locked PDF");
    m_title.set_text("“" + file + "” is locked");
    m_detail.set_text("Type its password to open it. Lookit keeps it only until you quit.");
    m_error.set_text("That password didn't open it.");
    m_error.set_visible(again);
    m_first.set_text("");
    m_first.property_placeholder_text() = "Password";
    m_second.set_visible(false);
    m_remove.set_visible(false);
    m_ok.set_label("Open");
    present();
    m_first.grab_focus();
}

void PasswordDialog::ask_protect(const std::string& file, bool locked, Done done) {
    m_protect = true;
    m_done = std::move(done);
    set_title("Password");
    m_title.set_text(locked ? "“" + file + "” opens with a password"
                            : "Protect “" + file + "” with a password");
    m_detail.set_text(locked ? "Type a new password to change it, or remove it. Saving writes the change."
                             : "Anyone opening the saved PDF will need it. Saving writes it (AES-256).");
    m_error.set_visible(false);
    m_first.set_text("");
    m_second.set_text("");
    m_first.property_placeholder_text() = "New password";
    m_second.property_placeholder_text() = "The same again";
    m_second.set_visible(true);
    m_remove.set_visible(locked);
    m_ok.set_label(locked ? "Change" : "Protect");
    present();
    m_first.grab_focus();
}

void PasswordDialog::on_primary() {
    const std::string a = m_first.get_text();
    if (a.empty()) {
        m_error.set_text(m_protect ? "Type a password first." : "Type the password first.");
        m_error.set_visible(true);
        return;
    }
    if (m_protect && a != std::string(m_second.get_text())) {
        m_error.set_text("The two passwords don't match.");
        m_error.set_visible(true);
        m_second.grab_focus();
        return;
    }
    finish(Answer::Password);
}

void PasswordDialog::finish(Answer a) {
    if (!m_done) { set_visible(false); return; }
    const std::string pw = a == Answer::Password ? std::string(m_first.get_text()) : std::string{};
    m_first.set_text("");
    m_second.set_text("");
    auto done = std::move(m_done);
    m_done = nullptr;
    set_visible(false);
    done(a, pw);   // may ask again (a wrong password): the dialog is ready for it
}

}  // namespace lookit
