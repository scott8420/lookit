#pragma once
#include <gtkmm/application.h>
#include <gtkmm/cssprovider.h>
#include <memory>
#include <string>

// App -- the Gtk::Application. Thin: stands up logging in create(), builds the
// Shell on activate (or on the first action that needs it). (CANON: thin bootstrap -- main -> App::create() -> Shell.)
namespace lookit {

class Shell;

class App : public Gtk::Application {
public:
    static Glib::RefPtr<App> create();

    // s014 -- the skin's base colour (core/Tint). set_tint re-tints the whole
    // stylesheet live and remembers it in ~/.config/lookit/settings.json;
    // remember=false previews without writing (the colour dialog's custom
    // picker while it is still open).
    const std::string& tint() const { return m_tint; }
    void set_tint(const std::string& hex, bool remember = true);
    static std::string settings_file();

protected:
    App();
    void on_activate() override;
    void on_startup() override;   // accelerators, driven from core::shortcut_registry()
    void on_open(const Gio::Application::type_vec_files& files, const Glib::ustring& hint) override;

private:
    // The QUICK-SETTINGS-TILE stone: text from outside -- the tile (D-Bus
    // ActivateAction "receive-text"), the desktop entry's "Send clipboard",
    // and `lookit --send TEXT` / `--send-clipboard`. All roads end at the same
    // two app actions, so the tile and the command line can't disagree.
    int  on_local_options(const Glib::RefPtr<Glib::VariantDict>& options);
    Shell& ensure_shell();        // one window, ever: built on first need, then reused

    Shell* m_shell = nullptr;   // owned by the application via add_window
    Glib::RefPtr<Gtk::CssProvider> m_css;
    std::string m_tint;
};

}  // namespace lookit
