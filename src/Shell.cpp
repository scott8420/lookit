#include "Shell.hpp"
#include "PasswordDialog.hpp"
#include "AboutWindow.hpp"      // complete type for ~unique_ptr<AboutWindow>
#include "ShortcutsDialog.hpp"
#include "SignatureDialog.hpp"   // complete type for ~unique_ptr<SignatureDialog>
#include "Log.hpp"
#include "core/Recents.hpp"

// Shell.cpp -- GLUE. The ctor and the ordered build_ui() pass; every other body
// lives in a category file (see the index in Shell.hpp).

namespace lookit {

Shell::Shell()
    : m_logo_button("shell.logo_button"),
      m_menu_button("shell.menu_button"),
      m_body("shell.body", Gtk::Orientation::HORIZONTAL),
      m_side("shell.side", Gtk::Orientation::VERTICAL),
      m_nav("shell.nav"),
      m_stack("shell.stack"),
      m_home("home.page", Gtk::Orientation::VERTICAL, 18),
      m_hero("home.hero", Gtk::Orientation::VERTICAL, 10),
      m_recents_flow("home.recents"),
      m_recents_head("home.recents_head"),
      m_view("view.page", Gtk::Orientation::VERTICAL),
      m_bar("view.bar", Gtk::Orientation::HORIZONTAL, 2),
      m_doc_name("view.doc_name"),
      m_zoom_label("view.zoom"),
      m_page_entry("view.page_entry"),
      m_page_of("view.page_of"),
      m_pages_group("view.pages_group", Gtk::Orientation::HORIZONTAL, 2),
      m_fit_width("view.fit_width"),
      m_fit_page("view.fit_page"),
      m_book("view.book"),
      m_view_empty("view.empty"),
      m_view_body("view.body", Gtk::Orientation::HORIZONTAL),
      m_find_rev("view.find"),
      m_find_bar("view.find_bar", Gtk::Orientation::HORIZONTAL, 4),
      m_find_count("view.find_count"),
      m_overlay("shell.drawers.overlay"),
      m_scrim("shell.drawers.scrim", Gtk::Orientation::VERTICAL),
      m_left_tab("shell.drawers.left_tab"),
      m_right_tab("shell.drawers.right_tab"),
      m_left_drawer("shell.drawers.left"),
      m_right_drawer("shell.drawers.right"),
      m_left_panel("shell.drawers.left_panel", Gtk::Orientation::VERTICAL, 8),
      m_right_panel("shell.drawers.right_panel", Gtk::Orientation::VERTICAL, 8),
      m_info("shell.drawers.info"),
      m_toast("shell.toast"),
      m_toast_label("shell.toast_label"),
      m_extract_rev("view.extract"),
      m_extract_bar("view.extract_bar", Gtk::Orientation::HORIZONTAL, 6),
      m_extract_entry("view.extract_entry"),
      m_extract_msg("view.extract_msg"),
      m_export_rev("view.export"),
      m_export_bar("view.export_bar", Gtk::Orientation::HORIZONTAL, 6),
      m_export_pages("view.export_pages"),
      m_export_msg("view.export_msg"),
      m_markup_rev("view.markup"),
      m_markup_bar("view.markup_bar", Gtk::Orientation::HORIZONTAL, 2),
      m_picture_rev("view.picture"),
      m_picture_bar("view.picture_bar", Gtk::Orientation::HORIZONTAL, 2) {
    set_name("shell.window");
}

Shell::~Shell() {
    if (m_text_pop) m_text_pop->unparent();   // a popover is parented by hand
    if (m_field_pop) m_field_pop->unparent();
    if (m_pic_idle.connected()) m_pic_idle.disconnect();
    if (m_pic_base) cairo_surface_destroy(m_pic_base);
    if (m_pic_shaped) cairo_surface_destroy(m_pic_shaped);
}

// Frame, pages, actions, keys, drops; then load the recents and show Home.
void Shell::build_ui() {
    build_shell();
    build_home();
    build_view();
    bind_actions();
    bind_keys();
    bind_drop();
    bind_edit();
    bind_sign();
    bind_markup();
    bind_picture();
    bind_forms();
    m_recents_file = recents_file();
    m_recents = core::load_recents(m_recents_file);
    rebuild_recents();
    sync_view_bar();
    go("home");
    if (auto lg = log::get(log::Area::Shell)) lg->info("ready -- {} recent(s)", m_recents.size());
}

}  // namespace lookit
