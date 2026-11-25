// ─────────────────────────────────────────────────────────────────────────────
// lookit-peek -- Lookit's quick look for Files: the spacebar previewer.
//
// Files (Nautilus 50) talks to whatever owns org.gnome.NautilusPreviewer on the
// session bus, at /org/gnome/NautilusPreviewer, interface
// org.gnome.NautilusPreviewer2 (from nautilus/src/nautilus-previewer.c):
//   ShowFile(s uri, s window_handle, b close_if_already_visible, s activation_token)
//   Close()
//   property  Visible (b)          -- read from the proxy's cache
//   signals   SelectionEvent(u)    -- a GtkDirectionType: Files moves its selection
//             NavigateTo(s), Rename(s), Trash(s)   -- declared; not sent yet
// Sushi is that owner by default. install-desktop.sh puts a per-user D-Bus
// service file for the name, which the session bus prefers over the system's,
// so Lookit answers instead; --uninstall removes it and Sushi is back.
//
// This is its own small program (not the Lookit window) because the bus name
// IS the application id: a GApplication called org.gnome.NautilusPreviewer
// owns the name and its object path is exactly /org/gnome/NautilusPreviewer.
// The interface is exported in dbus_register_vfunc -- BEFORE the name is
// taken -- so a ShowFile that D-Bus activation queued can't arrive first.
//
// Keys (Quick Look's): Space / Esc close, Enter opens in Lookit, arrows move
// Files' selection (Files then calls ShowFile with the next file).
// ─────────────────────────────────────────────────────────────────────────────
#include "Css.hpp"
#include "DocView.hpp"
#include "Document.hpp"
#include "Log.hpp"
#include "core/FileKind.hpp"
#include "core/Tint.hpp"

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/wayland/gdkwayland.h>
#endif
#include <giomm.h>
#include <glibmm.h>
#include <gtkmm.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

const char* kIntrospection = R"XML(
<node>
  <interface name="org.gnome.NautilusPreviewer2">
    <method name="ShowFile">
      <arg type="s" name="uri" direction="in"/>
      <arg type="s" name="window_handle" direction="in"/>
      <arg type="b" name="close_if_already_visible" direction="in"/>
      <arg type="s" name="activation_token" direction="in"/>
    </method>
    <method name="Close"/>
    <signal name="SelectionEvent"><arg type="u" name="direction"/></signal>
    <signal name="NavigateTo"><arg type="s" name="uri"/></signal>
    <signal name="Rename"><arg type="s" name="uri"/></signal>
    <signal name="Trash"><arg type="s" name="uri"/></signal>
    <property name="Visible" type="b" access="read"/>
  </interface>
</node>)XML";

constexpr const char* kIface = "org.gnome.NautilusPreviewer2";

std::string human_size(std::uintmax_t b) {
    const char* units[] = {"bytes", "KB", "MB", "GB", "TB"};
    double v = double(b);
    int u = 0;
    while (v >= 1024 && u < 4) { v /= 1024; ++u; }
    std::ostringstream o;
    if (u == 0) o << b << " bytes";
    else o << std::fixed << std::setprecision(v < 10 ? 1 : 0) << v << " " << units[u];
    return o.str();
}

// ── The window ──────────────────────────────────────────────────────────────
class PeekWindow : public Gtk::Window {
public:
    PeekWindow() {
        set_name("peek.window");
        add_css_class("lookit");
        add_css_class("page-view");
        add_css_class("peek");
        set_hide_on_close(true);
        set_default_size(640, 760);

        auto header = Gtk::make_managed<Gtk::HeaderBar>();
        m_title.add_css_class("title");
        m_title.set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        header->set_title_widget(m_title);
        m_open.set_label("Open in Lookit");
        m_open.add_css_class("peek-open");
        m_open.signal_clicked().connect([this] { open_in_lookit(); });
        header->pack_end(m_open);
        set_titlebar(*header);

        m_stack.set_transition_type(Gtk::StackTransitionType::NONE);
        m_stack.add(m_doc_view, "doc");
        m_text.set_editable(false);
        m_text.set_cursor_visible(false);
        m_text.set_monospace(true);
        m_text.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        m_text.set_margin(14);
        m_text.add_css_class("peek-text");
        m_text_scroll.set_child(m_text);
        m_stack.add(m_text_scroll, "text");
        m_card.set_valign(Gtk::Align::CENTER);
        m_card.set_halign(Gtk::Align::CENTER);
        m_card.add_css_class("hero-card");
        m_card.add_css_class("peek-card");
        m_icon.set_pixel_size(128);
        m_card_name.add_css_class("hero-title");
        m_card_name.add_css_class("peek-name");
        m_card_name.set_wrap(true);
        m_card_name.set_max_width_chars(30);
        m_card_name.set_justify(Gtk::Justification::CENTER);
        m_card_info.add_css_class("hero-sub");
        m_card_info.set_justify(Gtk::Justification::CENTER);
        m_card.append(m_icon);
        m_card.append(m_card_name);
        m_card.append(m_card_info);
        m_stack.add(m_card, "card");
        set_child(m_stack);

        // Quick Look's keys, heard FIRST (capture) so the page view's own
        // Space / arrows never see them.
        auto keys = Gtk::EventControllerKey::create();
        keys->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
        keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
            if ((st & (Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK)) != Gdk::ModifierType{})
                return false;
            switch (kv) {
                case GDK_KEY_space: case GDK_KEY_Escape: close(); return true;
                case GDK_KEY_Return: case GDK_KEY_KP_Enter: open_in_lookit(); return true;
                case GDK_KEY_Up:    m_select.emit(GTK_DIR_UP);    return true;
                case GDK_KEY_Down:  m_select.emit(GTK_DIR_DOWN);  return true;
                case GDK_KEY_Left:  m_select.emit(GTK_DIR_LEFT);  return true;
                case GDK_KEY_Right: m_select.emit(GTK_DIR_RIGHT); return true;
                default: return false;
            }
        }, false);
        add_controller(keys);
        property_visible().signal_changed().connect([this] { m_visible_changed.emit(get_visible()); });
    }

    const std::string& path() const { return m_path; }
    sigc::signal<void(guint)>& signal_select() { return m_select; }
    sigc::signal<void(bool)>& signal_visible_changed() { return m_visible_changed; }

    void show_file(const std::string& path, const std::string& handle, const std::string& token) {
        m_path = path;
        m_title.set_text(fs::path(path).filename().string());
        const auto kind = lookit::core::detect(path);
        std::string error;
        std::unique_ptr<lookit::Document> doc;
        if (!fs::is_directory(path) && (kind == lookit::core::Kind::Pdf || kind == lookit::core::Kind::Image))
            doc = lookit::Document::open(path, error);
        if (doc) {
            std::shared_ptr<lookit::Document> shared = std::move(doc);
            size_for(*shared);
            m_stack.set_visible_child("doc");
            m_doc_view.set_document(shared);
            m_open.set_visible(true);
        } else if (!fs::is_directory(path) && show_text(path)) {
            set_default_size(680, 720);
            m_stack.set_visible_child("text");
            m_open.set_visible(false);
        } else {
            show_card(path);
            set_default_size(440, 380);
            m_stack.set_visible_child("card");
            m_open.set_visible(false);
        }
        if (!token.empty()) set_startup_id(token);   // Wayland: lets us take focus
        present();
        set_parent_handle(handle);
    }

private:
    // Pages: a portrait sheet's shape; pictures: their own, within reason.
    void size_for(const lookit::Document& d) {
        const auto s = d.size(0);
        if (s.w <= 0 || s.h <= 0) return;
        const double maxw = 900, maxh = 820, minside = 320;
        double k = std::min(maxw / s.w, maxh / s.h);
        if (d.kind() == lookit::core::Kind::Image) k = std::min(k, 1.0);
        const int w = std::max(int(s.w * k) + 48, int(minside));
        const int h = std::max(int(s.h * k) + 96, int(minside));
        set_default_size(w, h);
    }

    bool show_text(const std::string& path) {
        auto f = Gio::File::create_for_path(path);
        std::string type;
        try {
            type = f->query_info(G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE)->get_content_type();
        } catch (const Glib::Error&) {
            return false;
        }
        if (!Gio::content_type_is_a(type, "text/plain")) return false;
        std::ifstream in(path, std::ios::binary);
        std::string buf(256 * 1024, '\0');
        in.read(buf.data(), std::streamsize(buf.size()));
        buf.resize(std::size_t(in.gcount()));
        const bool cut = !in.eof();
        // Trim a split UTF-8 sequence at the end, then insist on valid text.
        while (!buf.empty() && (buf.back() & 0xC0) == 0x80) buf.pop_back();
        if (!buf.empty() && (buf.back() & 0x80)) buf.pop_back();
        if (!g_utf8_validate(buf.c_str(), gssize(buf.size()), nullptr)) return false;
        if (cut) buf += "\n\n[...the first 256 KB]";
        m_text.get_buffer()->set_text(buf);
        return true;
    }

    void show_card(const std::string& path) {
        auto f = Gio::File::create_for_path(path);
        std::string info;
        try {
            auto fi = f->query_info("standard::*,time::modified");
            m_icon.set(fi->get_icon());
            m_card_name.set_text(fi->get_display_name());
            if (fi->get_file_type() == Gio::FileType::DIRECTORY) {
                int n = 0;
                try {
                    auto en = f->enumerate_children("standard::name");
                    while (en->next_file() && n < 100000) ++n;
                } catch (const Glib::Error&) {}
                info = "Folder · " + std::to_string(n) + (n == 1 ? " item" : " items");
            } else {
                info = Gio::content_type_get_description(fi->get_content_type()) + " · " +
                       human_size(std::uintmax_t(fi->get_size()));
            }
            if (auto dt = fi->get_modification_date_time())
                info += "\nModified " + dt.to_local().format("%e %b %Y, %H:%M");
        } catch (const Glib::Error& e) {
            m_icon.set_from_icon_name("dialog-question-symbolic");
            m_card_name.set_text(fs::path(path).filename().string());
            info = e.what();
        }
        m_card_info.set_text(info);
    }

    // Wayland: sit over Files' window (it sends "wayland:<exported handle>").
    void set_parent_handle(const std::string& handle) {
#ifdef GDK_WINDOWING_WAYLAND
        if (handle.rfind("wayland:", 0) != 0) return;
        auto* surface = gtk_native_get_surface(GTK_NATIVE(gobj()));
        if (surface && GDK_IS_WAYLAND_TOPLEVEL(surface))
            gdk_wayland_toplevel_set_transient_for_exported(GDK_WAYLAND_TOPLEVEL(surface),
                                                            handle.substr(8).c_str());
#else
        (void)handle;
#endif
    }

    void open_in_lookit() {
        if (m_path.empty()) return;
        // Lookit sits next to us (build/ or ~/.local/lib/lookit/).
        std::string self;
        try { self = fs::read_symlink("/proc/self/exe").string(); } catch (...) {}
        const auto lookit = fs::path(self).parent_path() / "lookit";
        try {
            Glib::spawn_async("", std::vector<std::string>{fs::exists(lookit) ? lookit.string() : "lookit", m_path},
                              Glib::SpawnFlags::SEARCH_PATH);
        } catch (const Glib::Error& e) {
            if (auto lg = lookit::log::get(lookit::log::Area::App)) lg->error("peek: launch failed: {}", e.what());
        }
        close();
    }

    Gtk::Label m_title;
    Gtk::Button m_open;
    Gtk::Stack m_stack;
    lookit::DocView m_doc_view;
    Gtk::ScrolledWindow m_text_scroll;
    Gtk::TextView m_text;
    Gtk::Box m_card{Gtk::Orientation::VERTICAL, 10};
    Gtk::Image m_icon;
    Gtk::Label m_card_name, m_card_info;
    std::string m_path;
    sigc::signal<void(guint)> m_select;
    sigc::signal<void(bool)> m_visible_changed;
};

// ── The service ─────────────────────────────────────────────────────────────
class PeekApp : public Gtk::Application {
public:
    PeekApp() : Gtk::Application("org.gnome.NautilusPreviewer", Gio::Application::Flags::IS_SERVICE) {
        set_inactivity_timeout(60 * 1000);   // gone a minute after the last peek
    }

protected:
    bool dbus_register_vfunc(const Glib::RefPtr<Gio::DBus::Connection>& conn,
                             const Glib::ustring& path) override {
        if (!Gtk::Application::dbus_register_vfunc(conn, path)) return false;
        m_conn = conn;
        m_path = path;
        auto node = Gio::DBus::NodeInfo::create_for_xml(kIntrospection);
        m_vtable = std::make_unique<Gio::DBus::InterfaceVTable>(
            sigc::mem_fun(*this, &PeekApp::on_method), sigc::mem_fun(*this, &PeekApp::on_get_property));
        m_reg = conn->register_object(path, node->lookup_interface(kIface), *m_vtable);
        if (auto lg = lookit::log::get(lookit::log::Area::App)) lg->info("peek: serving {} at {}", kIface, path.raw());
        return true;
    }

    void dbus_unregister_vfunc(const Glib::RefPtr<Gio::DBus::Connection>& conn,
                               const Glib::ustring& path) override {
        if (m_reg) conn->unregister_object(m_reg);
        m_reg = 0;
        Gtk::Application::dbus_unregister_vfunc(conn, path);
    }

    void on_startup() override {
        Gtk::Application::on_startup();
        if (auto settings = Gtk::Settings::get_default())
            settings->property_gtk_application_prefer_dark_theme() = true;
        auto css = Gtk::CssProvider::create();
        // s014: quick look wears the same base colour as the app.
        const char* env = std::getenv("LOOKIT_TINT");
        std::string tint = env ? lookit::core::normalise_hex(env) : std::string();
        if (tint.empty())
            tint = lookit::core::load_tint((std::filesystem::path(Glib::get_user_config_dir()) /
                                            "lookit" / "settings.json").string());
        css->load_from_data(lookit::core::tint_css(std::string(lookit::LOOKIT_CSS) + R"CSS(
            window.peek headerbar { min-height: 36px; }
            window.lookit headerbar button.peek-open {
                padding: 4px 14px; border-radius: 999px; font-weight: 800;
                color: #053039; background-image: none; background-color: #99e9f2; }
            window.lookit headerbar button.peek-open:hover { background-color: #c5f6fa; }
            .peek-text, .peek-text text { background-color: rgba(0,0,0,0.25); color: #eef8fa; }
            .peek-card { padding: 28px 36px; }
            .peek-name { font-size: 1.4em; }
        )CSS", tint));
        Gtk::StyleContext::add_provider_for_display(Gdk::Display::get_default(), css,
                                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    void on_activate() override {}   // `lookit-peek` alone does nothing; Files calls ShowFile

private:
    PeekWindow& window() {
        if (!m_win) {
            m_win = std::make_unique<PeekWindow>();
            m_win->signal_select().connect([this](guint dir) { emit("SelectionEvent", Glib::Variant<guint32>::create(dir)); });
            m_win->signal_visible_changed().connect([this](bool v) {
                if (v && !m_held) { hold(); m_held = true; }      // live while showing
                if (!v && m_held) { release(); m_held = false; }  // then the inactivity clock
                notify_visible(v);
            });
        }
        return *m_win;
    }

    void on_method(const Glib::RefPtr<Gio::DBus::Connection>&, const Glib::ustring&, const Glib::ustring&,
                   const Glib::ustring&, const Glib::ustring& method,
                   const Glib::VariantContainerBase& params,
                   const Glib::RefPtr<Gio::DBus::MethodInvocation>& inv) {
        if (method == "ShowFile") {
            Glib::Variant<Glib::ustring> uri, handle, token;
            Glib::Variant<bool> close_if;
            params.get_child(uri, 0);
            params.get_child(handle, 1);
            params.get_child(close_if, 2);
            params.get_child(token, 3);
            std::string path;
            try { path = Glib::filename_from_uri(uri.get()); } catch (const Glib::Error&) {}
            if (auto lg = lookit::log::get(lookit::log::Area::App))
                lg->info("peek: ShowFile {} (close_if_visible={})", uri.get().raw(), close_if.get());
            auto& w = window();
            if (close_if.get() && w.get_visible() && w.path() == path) w.close();
            else if (!path.empty()) w.show_file(path, handle.get(), token.get());
            inv->return_value(Glib::VariantContainerBase());
        } else if (method == "Close") {
            if (m_win) m_win->close();
            inv->return_value(Glib::VariantContainerBase());
        } else {
            inv->return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", "No such method");
        }
    }

    void on_get_property(Glib::VariantBase& out, const Glib::RefPtr<Gio::DBus::Connection>&,
                         const Glib::ustring&, const Glib::ustring&, const Glib::ustring&,
                         const Glib::ustring& name) {
        if (name == "Visible") out = Glib::Variant<bool>::create(m_win && m_win->get_visible());
    }

    void emit(const char* signal, const Glib::VariantBase& arg) {
        if (!m_conn) return;
        m_conn->emit_signal(m_path, kIface, signal, "", Glib::VariantContainerBase::create_tuple(arg));
    }

    // Files reads Visible from its proxy's CACHE, so the change has to be
    // announced (PropertiesChanged) or Files never learns we closed.
    void notify_visible(bool v) {
        if (!m_conn) return;
        std::map<Glib::ustring, Glib::VariantBase> changed{{"Visible", Glib::Variant<bool>::create(v)}};
        std::vector<Glib::ustring> invalidated;
        auto args = Glib::VariantContainerBase::create_tuple(
            {Glib::Variant<Glib::ustring>::create(kIface),
             Glib::Variant<std::map<Glib::ustring, Glib::VariantBase>>::create(changed),
             Glib::Variant<std::vector<Glib::ustring>>::create(invalidated)});
        m_conn->emit_signal(m_path, "org.freedesktop.DBus.Properties", "PropertiesChanged", "", args);
    }

    Glib::RefPtr<Gio::DBus::Connection> m_conn;
    Glib::ustring m_path;
    std::unique_ptr<Gio::DBus::InterfaceVTable> m_vtable;
    guint m_reg = 0;
    std::unique_ptr<PeekWindow> m_win;
    bool m_held = false;
};

}  // namespace

int main(int argc, char** argv) {
    lookit::log::init();
    auto app = Glib::make_refptr_for_instance(new PeekApp());
    return app->run(argc, argv);
}
