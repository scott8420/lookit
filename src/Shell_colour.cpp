// Shell_colour -- the base colour of the skin, as the window sees it (s014).
// App owns the stylesheet and the setting (App::set_tint); the window only
// repaints what CSS can't reach: the glassy logo on Home.
#include "Shell.hpp"
#include "App.hpp"
#include "ColourDialog.hpp"
#include "Log.hpp"
#include "core/Tint.hpp"

#include <gdkmm/pixbufloader.h>
#include <gdkmm/texture.h>
#include <giomm/resource.h>
#include <gtkmm/image.h>


namespace lookit {

namespace {
constexpr const char* kLogoPath = "/io/github/scott8420/Lookit/icons/scalable/apps/io.github.scott8420.Lookit.svg";
}

// The logo is an SVG of colour literals, so the same turn that tints the
// stylesheet tints it (core::tint_css works on any text). Rendered at 2x so it
// stays crisp on a HiDPI screen.
void Shell::show_tint(const std::string& hex) {  // colour: repaint the logo
    if (!m_hero_logo) return;
    // The logo follows the colour (a teal disc on rose read as a mistake).
    // The desktop icon stays teal: it is the app's name on the desktop.
    if (hex == core::kDefaultTint) {
        m_hero_logo->set_from_icon_name("io.github.scott8420.Lookit");
        m_hero_logo->set_pixel_size(112);
        return;
    }
    try {
        auto bytes = Gio::Resource::lookup_data_global(kLogoPath);
        gsize n = 0;
        const char* data = static_cast<const char*>(bytes->get_data(n));
        const std::string svg = core::tint_css(std::string(data, n), hex);
        auto loader = Gdk::PixbufLoader::create();
        loader->set_size(224, 224);
        loader->write(reinterpret_cast<const guint8*>(svg.data()), svg.size());
        loader->close();
        m_hero_logo->set(Gdk::Texture::create_for_pixbuf(loader->get_pixbuf()));
        m_hero_logo->set_pixel_size(112);
    } catch (const Glib::Error& e) {
        if (auto lg = log::get(log::Area::Shell)) lg->warn("logo tint: {}", e.what());
    }
}

// Menu -> Colour…: the dialog asks App for the colour and hands picks back to
// it, so the setting has one owner.
void Shell::on_colour() {  // colour: the dialog (hide-on-close singleton)
    auto* app = dynamic_cast<App*>(get_application().get());
    if (!app) return;
    if (!m_colour)
        m_colour = std::make_unique<ColourDialog>(
            *this, [app] { return app->tint(); },
            [app](const std::string& hex) { app->set_tint(hex); });
    m_colour->show_current();
    m_colour->present();
}

}  // namespace lookit
