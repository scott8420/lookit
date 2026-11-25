#include "Shell.hpp"
#include "Export.hpp"
#include "Log.hpp"
#include "SignatureDialog.hpp"
#include "MarkArt.hpp"
#include "core/Sign.hpp"

#include <giomm/file.h>
#include <glibmm/main.h>
#include <gtkmm/button.h>
#include <gtkmm/filedialog.h>
#include <gtkmm/label.h>
#include <gtkmm/stringlist.h>

#include <filesystem>

// Shell_sign.cpp -- SIGN and EXPORT (s008).
//
// Sign: the signature is drawn once (SignatureDialog) and kept in Lookit's
// data; Add Signature puts it on the page you're on, upright to you. On the
// page it's selected with a click, moved by dragging, sized by its corner,
// removed with Delete -- each one undo step through core::pages' mark verbs
// (the ONE door). A signature is a mark (core/Marks); moving / sizing /
// deleting marks lives in Shell_markup.cpp. Save stamps it into the PDF.
//
// Export: a pill under the toolbar -- format, pages, dpi -- then a file (one
// page, text, a picture) or a folder (many pages, one file each, named
// "<name>-p01.png"; a name already taken gets " (2)"). Pages are written one
// per idle slice so the window stays live, with progress in a toast.

namespace lookit {

namespace fs = std::filesystem;
namespace P = core::pages;
namespace E = exporter;

void Shell::bind_sign() {  // sign: actions, the export bar, view signals
    m_act_sign = add_action("sign", sigc::mem_fun(*this, &Shell::on_sign));
    add_action("signature", sigc::mem_fun(*this, &Shell::on_signature));
    m_act_export = add_action("export", sigc::mem_fun(*this, &Shell::on_export));

    // The signature button, beside the Pages menu.
    auto sign = Gtk::make_managed<Gtk::Button>();
    sign->set_name("view.sign");
    sign->set_icon_name("lookit-sign-symbolic");
    sign->set_tooltip_text("Add your signature to this page (Ctrl+Shift+I)");
    sign->add_css_class("flat");
    sign->set_action_name("win.sign");
    auto* info = m_bar.get_last_child();   // keep Info last
    m_bar.insert_child_after(*sign, *info->get_prev_sibling()->get_prev_sibling());

    // The export bar: a pill under the extract bar.
    m_export_bar.add_css_class("view-bar");
    m_export_bar.set_halign(Gtk::Align::CENTER);
    auto lbl = Gtk::make_managed<Gtk::Label>("Export");
    lbl->add_css_class("doc-name");
    m_export_format.set_name("view.export_format");
    m_export_format.property_selected().signal_changed().connect([this] { sync_export_bar(); });
    m_export_pages.set_placeholder_text("all pages");
    m_export_pages.set_width_chars(12);
    m_export_pages.signal_activate().connect(sigc::mem_fun(*this, &Shell::on_export_run));
    m_export_dpi.set_name("view.export_dpi");
    std::vector<Glib::ustring> dpis;
    for (int d : E::dpis()) dpis.push_back(std::to_string(d) + " dpi");
    m_export_dpi.set_model(Gtk::StringList::create(dpis));
    m_export_dpi.set_selected(1);   // 150
    auto go_btn = Gtk::make_managed<Gtk::Button>("Export…");
    go_btn->add_css_class("suggested-action");
    go_btn->signal_clicked().connect(sigc::mem_fun(*this, &Shell::on_export_run));
    auto close = Gtk::make_managed<Gtk::Button>();
    close->set_icon_name("window-close-symbolic");
    close->signal_clicked().connect([this] { m_export_rev.set_reveal_child(false); });
    m_export_msg.add_css_class("page-label");
    m_export_bar.append(*lbl);
    m_export_bar.append(m_export_format);
    m_export_bar.append(m_export_pages);
    m_export_bar.append(m_export_dpi);
    m_export_bar.append(*go_btn);
    m_export_bar.append(m_export_msg);
    m_export_bar.append(*close);
    m_export_rev.set_child(m_export_bar);
    m_export_rev.set_reveal_child(false);
    m_view.insert_child_after(m_export_rev, m_extract_rev);

    m_doc_view.signal_changed().connect([this] {
        if (m_act_sign) m_act_sign->set_enabled(m_editing);
        if (m_act_export) m_act_export->set_enabled(bool(m_doc_view.document()));
    });
    m_act_sign->set_enabled(false);
    m_act_export->set_enabled(false);
}

// ── Sign ─────────────────────────────────────────────────────────────────────

void Shell::on_sign() {  // sign: Add Signature (draw it first if none)
    if (!m_editing) {
        show_toast("Signatures go on PDFs. (Combine a picture into a PDF first.)");
        return;
    }
    const std::string art = SignatureDialog::latest();
    if (art.empty()) {
        if (!m_signature) m_signature = std::make_unique<SignatureDialog>(*this);
        m_signature->ask("", [this](const std::string& path) { place_signature(path); });
        return;
    }
    place_signature(art);
}

void Shell::on_signature() {  // sign: Signature... -- draw / import / replace
    if (!m_signature) m_signature = std::make_unique<SignatureDialog>(*this);
    m_signature->ask(SignatureDialog::latest(), [this](const std::string& path) {
        if (m_editing) place_signature(path);
        else show_toast("Signature kept. Open a PDF and use Add Signature.");
    });
}

void Shell::place_signature(const std::string& art) {  // sign: onto the page you're on
    if (!m_editing) return;
    const int page = std::max(0, m_doc_view.page());
    const auto id = m_pages.id_at(page);
    const auto rec = m_pages.get(id);
    const auto [aw, ah] = mark_art::size(art);
    if (!rec || aw <= 0) { show_toast("Couldn't read the signature picture.", true); return; }
    const auto b = m_doc_view.document()->base(page);
    if (!P::add_mark(m_journal, m_pages, id, core::marks::signature(art, aw, ah, b.w, b.h, rec->turn))) return;
    m_doc_view.set_tool(DocView::Tool::Select);
    sync_markup_bar();
    m_doc_view.select_mark(page, int(m_pages.get(id)->marks.size()) - 1);
    show_toast("Drag to move it, its corner to size it. Delete removes it.");
}

// ── Export ───────────────────────────────────────────────────────────────────

void Shell::on_export() {  // sign: show the export bar
    const auto& doc = m_doc_view.document();
    if (!doc) return;
    go("view");
    std::vector<Glib::ustring> names;
    for (auto f : E::formats_for(*doc)) names.push_back(E::label(f));
    m_export_format.set_model(Gtk::StringList::create(names));
    m_export_format.set_selected(0);
    m_export_pages.set_text("");
    sync_export_bar();
    sync_edit_ui();   // go() repaints the name; keep the unsaved dot
    m_export_rev.set_reveal_child(true);
    m_export_format.grab_focus();
}

void Shell::sync_export_bar() {  // sign: formats for this kind; dpi / pages greys
    const auto& doc = m_doc_view.document();
    if (!doc) return;
    const auto fmts = E::formats_for(*doc);
    const auto sel = m_export_format.get_selected();
    const auto f = sel < fmts.size() ? fmts[sel] : fmts.front();
    const bool picture = doc->kind() == core::Kind::Image;
    m_export_pages.set_visible(!picture && doc->count() > 1);
    m_export_dpi.set_visible(!picture && f != E::Format::Text);
    m_export_msg.set_text(picture ? std::to_string(int(doc->size(0).w)) + " x " +
                                        std::to_string(int(doc->size(0).h)) + " px"
                                  : "of " + std::to_string(doc->count()));
}

void Shell::on_export_run() {  // sign: ranges + format -> where -> start
    const auto doc = m_doc_view.document();
    if (!doc) return;
    const auto fmts = E::formats_for(*doc);
    const auto sel = m_export_format.get_selected();
    const auto f = sel < fmts.size() ? fmts[sel] : fmts.front();
    std::vector<int> pages;
    if (doc->kind() == core::Kind::Image || m_export_pages.get_text().empty() || !m_export_pages.get_visible()) {
        for (int i = 0; i < doc->count(); ++i) pages.push_back(i);
    } else {
        std::string err;
        pages = P::parse_ranges(m_export_pages.get_text(), doc->count(), err);
        if (!err.empty() || pages.empty()) { m_export_msg.set_text(err.empty() ? "which pages?" : err); return; }
    }
    const auto dsel = m_export_dpi.get_selected();
    m_job = {};
    m_job.doc = doc;
    m_job.pages = pages;
    m_job.format = int(f);
    m_job.dpi = dsel < E::dpis().size() ? E::dpis()[dsel] : 150;
    const std::string base = doc->kind() == core::Kind::Image ? doc->path()
                             : m_doc_path.empty()            ? std::string("Untitled.pdf")
                                                              : m_doc_path;
    m_job.stem = fs::path(base).stem().string();
    const std::string folder = fs::path(base).has_parent_path() && !m_doc_path.empty()
                                   ? fs::path(base).parent_path().string()
                                   : doc->kind() == core::Kind::Image ? fs::path(base).parent_path().string() : "";

    auto dialog = Gtk::FileDialog::create();
    if (!folder.empty()) dialog->set_initial_folder(Gio::File::create_for_path(folder));
    const bool many = f != E::Format::Text && doc->kind() != core::Kind::Image && pages.size() > 1;
    if (many) {
        dialog->set_title("Export " + std::to_string(pages.size()) + " pages as " + E::label(f) + " into");
        dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
            Glib::RefPtr<Gio::File> d;
            try { d = dialog->select_folder_finish(res); } catch (const Glib::Error&) { return; }
            start_export(d->get_path(), true);
        });
        return;
    }
    std::string name;
    if (f == E::Format::Text || doc->kind() == core::Kind::Image) name = m_job.stem + "." + E::ext(f);
    else name = core::convert::page_name(m_job.stem, pages.front(), doc->count(), E::ext(f));
    dialog->set_title(std::string("Export as ") + E::label(f));
    dialog->set_initial_name(name);
    dialog->save(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& res) {
        Glib::RefPtr<Gio::File> out;
        try { out = dialog->save_finish(res); } catch (const Glib::Error&) { return; }
        start_export(out->get_path(), false);
    });
}

void Shell::start_export(const std::string& target, bool folder) {  // sign
    m_export_idle.disconnect();
    m_job.target = target;
    m_job.folder = folder;
    m_job.next = 0;
    m_job.written = 0;
    m_export_rev.set_reveal_child(false);
    const auto f = E::Format(m_job.format);
    std::string err;
    // One file, written now: text, a picture, a picture as PDF.
    if (f == E::Format::Text || m_job.doc->kind() == core::Kind::Image) {
        const bool ok = f == E::Format::Text  ? E::write_text(*m_job.doc, m_job.pages, target, err)
                        : f == E::Format::Pdf ? E::write_picture_pdf(*m_job.doc, target, err)
                                              : E::write_page(*m_job.doc, 0, m_job.dpi, f, target, err);
        if (ok) show_toast("Exported " + fs::path(target).filename().string());
        else show_toast("Export failed: " + err, true);
        if (auto lg = log::get(log::Area::Io)) lg->info("export {} -> {} ({})", E::label(f), target, ok ? "ok" : err);
        return;
    }
    // The folder chooser hands back a typed name that isn't there yet: make it.
    std::error_code ec;
    fs::create_directories(target, ec);
    show_toast("Exporting " + std::to_string(m_job.pages.size()) + " page(s)…");
    m_export_idle = Glib::signal_idle().connect(sigc::mem_fun(*this, &Shell::export_some));
}

bool Shell::export_some() {  // sign: one page per idle slice; false = done
    const auto f = E::Format(m_job.format);
    if (!m_job.doc || m_job.next >= m_job.pages.size()) return false;
    const int page = m_job.pages[m_job.next++];
    std::string path = m_job.target;
    if (m_job.folder) {
        path = core::convert::unique_name(
            (fs::path(m_job.target) / core::convert::page_name(m_job.stem, page, m_job.doc->count(), E::ext(f))).string(),
            [](const std::string& p) { std::error_code ec; return fs::exists(p, ec); });
    }
    std::string err;
    if (!E::write_page(*m_job.doc, page, m_job.dpi, f, path, err)) {
        show_toast("Export stopped at page " + std::to_string(page + 1) + ": " + err, true);
        if (auto lg = log::get(log::Area::Io)) lg->warn("export page {} -> {}: {}", page + 1, path, err);
        return false;
    }
    ++m_job.written;
    m_job.last = path;
    const bool done = m_job.next >= m_job.pages.size();
    if (done) {
        const std::string where = m_job.folder ? fs::path(m_job.target).filename().string()
                                               : fs::path(path).filename().string();
        show_toast("Exported " + std::to_string(m_job.written) + " page(s) as " + E::label(f) + " -- " + where);
        if (auto lg = log::get(log::Area::Io))
            lg->info("export {} page(s) {} @ {} dpi -> {}", m_job.written, E::label(f), m_job.dpi, m_job.target);
        m_job.doc.reset();
        return false;
    }
    show_toast("Exporting page " + std::to_string(m_job.written + 1) + " of " + std::to_string(m_job.pages.size()) + "…");
    return true;
}

}  // namespace lookit
