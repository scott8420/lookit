#include "Shell.hpp"
#include "Log.hpp"
#include "MarkArt.hpp"
#include "core/Marks.hpp"

#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>

// Shell_markup.cpp -- MARKUP (s009).
//
// The markup bar (Ctrl+Shift+A, or Pages menu -> Markup): a pill under the
// toolbar with the tools -- Select, Highlight, Note, Box, Ellipse, Line,
// Arrow, Text -- and the colours. The page view (DocView) does the pointer
// work and hands back finished marks; every change goes through core::pages'
// mark verbs, the ONE door, so each is one undo step:
//   a new mark          -> add_mark   ("Add arrow")
//   a drag / its corner -> set_mark   ("Move arrow", "Resize box")
//   a colour, a text    -> set_mark   ("Recolour box", "Edit note")
//   Delete              -> remove_mark ("Delete highlight")
// Notes and text boxes open a small editor first; an empty one is not made
// (and emptying one deletes it). Highlight stays the tool until you change it,
// so you can sweep line after line; the other tools go back to Select with
// the new mark selected, ready to adjust.

namespace lookit {

namespace P = core::pages;
namespace M = core::marks;
using Tool = DocView::Tool;

namespace {
core::MarkKind kind_of(Tool t) {
    switch (t) {
        case Tool::Highlight: return core::MarkKind::Highlight;
        case Tool::Note:      return core::MarkKind::Note;
        case Tool::Box:       return core::MarkKind::Box;
        case Tool::Ellipse:   return core::MarkKind::Ellipse;
        case Tool::Line:      return core::MarkKind::Line;
        case Tool::Arrow:     return core::MarkKind::Arrow;
        case Tool::Text:      return core::MarkKind::Text;
        default:              return core::MarkKind::Box;
    }
}
}  // namespace

void Shell::bind_markup() {  // markup: the bar, actions, view signals
    m_act_markup = add_action("markup", sigc::mem_fun(*this, &Shell::on_markup));

    m_markup_bar.add_css_class("view-bar");
    m_markup_bar.set_halign(Gtk::Align::CENTER);
    const std::vector<std::tuple<Tool, const char*, const char*>> tools{
        {Tool::Select,    "lookit-tool-select-symbolic",    "Select -- move, size, double-click to edit"},
        {Tool::Highlight, "lookit-tool-highlight-symbolic", "Highlight -- sweep over text"},
        {Tool::Note,      "lookit-tool-note-symbolic",      "Note -- click where it goes"},
        {Tool::Box,       "lookit-tool-box-symbolic",       "Box"},
        {Tool::Ellipse,   "lookit-tool-ellipse-symbolic",   "Ellipse"},
        {Tool::Line,      "lookit-tool-line-symbolic",      "Line"},
        {Tool::Arrow,     "lookit-tool-arrow-symbolic",     "Arrow"},
        {Tool::Text,      "lookit-tool-text-symbolic",      "Text -- click, or drag the width"},
    };
    for (const auto& [t, icon, tip] : tools) {
        auto* b = Gtk::make_managed<Gtk::ToggleButton>();
        b->set_icon_name(icon);
        b->set_tooltip_text(tip);
        b->add_css_class("flat");
        b->signal_toggled().connect([this, t = t, b] {
            if (m_markup_syncing) return;
            if (b->get_active()) set_markup_tool(t);
            else sync_markup_bar();   // a toggle can't be switched off by itself
        });
        m_tool_buttons.push_back({t, b});
        m_markup_bar.append(*b);
    }
    m_markup_bar.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));
    for (std::uint32_t c : M::palette()) {
        auto* b = Gtk::make_managed<Gtk::ToggleButton>();
        auto* dot = Gtk::make_managed<Gtk::DrawingArea>();
        dot->set_content_width(14);
        dot->set_content_height(14);
        dot->set_draw_func([c](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            cr->arc(w / 2.0, h / 2.0, std::min(w, h) / 2.0 - 1, 0, 2 * G_PI);
            cr->set_source_rgb(((c >> 24) & 0xff) / 255.0, ((c >> 16) & 0xff) / 255.0, ((c >> 8) & 0xff) / 255.0);
            cr->fill_preserve();
            cr->set_source_rgba(1, 1, 1, 0.6);
            cr->set_line_width(1);
            cr->stroke();
        });
        b->set_child(*dot);
        b->add_css_class("flat");
        b->set_tooltip_text("Colour (recolours the selected mark)");
        b->signal_toggled().connect([this, c, b] {
            if (m_markup_syncing) return;
            if (b->get_active()) set_markup_color(c);
            else sync_markup_bar();
        });
        m_swatches.push_back({c, b});
        m_markup_bar.append(*b);
    }
    auto close = Gtk::make_managed<Gtk::Button>();
    close->set_icon_name("window-close-symbolic");
    close->set_tooltip_text("Close the markup bar (back to Select)");
    close->signal_clicked().connect(sigc::mem_fun(*this, &Shell::on_markup));
    m_markup_bar.append(*close);
    m_markup_rev.set_child(m_markup_bar);
    m_markup_rev.set_reveal_child(false);
    m_view.insert_child_after(m_markup_rev, m_export_rev);

    // The note / text editor.
    m_text_pop = std::make_unique<Gtk::Popover>();
    m_text_pop->set_parent(m_doc_view);
    m_text_pop->set_autohide(true);
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(6);
    auto* sw = Gtk::make_managed<Gtk::ScrolledWindow>();
    sw->set_size_request(280, 110);
    m_text_view = Gtk::make_managed<Gtk::TextView>();
    m_text_view->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_text_view->set_left_margin(4);
    m_text_view->set_top_margin(4);
    sw->set_child(*m_text_view);
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    m_text_delete = Gtk::make_managed<Gtk::Button>("Delete");
    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    auto* hint = Gtk::make_managed<Gtk::Label>("Ctrl+Enter");
    hint->add_css_class("dim-label");
    auto* done = Gtk::make_managed<Gtk::Button>("Done");
    done->add_css_class("suggested-action");
    row->append(*m_text_delete);
    row->append(*spacer);
    row->append(*hint);
    row->append(*done);
    box->append(*sw);
    box->append(*row);
    m_text_pop->set_child(*box);
    done->signal_clicked().connect([this] { m_text_pop->popdown(); });
    m_text_delete->signal_clicked().connect([this] {
        m_text_view->get_buffer()->set_text("");   // empty = delete, on close
        m_text_pop->popdown();
    });
    auto keys = Gtk::EventControllerKey::create();
    keys->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType st) {
        if ((kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) &&
            (st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{}) {
            m_text_pop->popdown();
            return true;
        }
        return false;
    }, false);
    m_text_view->add_controller(keys);
    m_text_pop->signal_closed().connect(sigc::mem_fun(*this, &Shell::commit_text));

    m_doc_view.signal_mark_create().connect(sigc::mem_fun(*this, &Shell::on_mark_create));
    m_doc_view.signal_mark_edit().connect(sigc::mem_fun(*this, &Shell::on_mark_edit));
    m_doc_view.signal_mark_open().connect(sigc::mem_fun(*this, &Shell::on_mark_open));
    m_doc_view.signal_tool_reset().connect(sigc::mem_fun(*this, &Shell::sync_markup_bar));
    m_doc_view.signal_changed().connect([this] { if (m_act_markup) m_act_markup->set_enabled(m_editing); });
    m_act_markup->set_enabled(false);
    sync_markup_bar();
}

void Shell::on_markup() {  // markup: show / hide the bar
    if (!m_editing && !m_markup_rev.get_reveal_child()) {
        show_toast("Markup goes on PDFs.");
        return;
    }
    const bool show = !m_markup_rev.get_reveal_child();
    m_markup_rev.set_reveal_child(show);
    if (show) go("view");
    else m_doc_view.set_tool(Tool::Select);
    sync_markup_bar();
    sync_edit_ui();
}

void Shell::set_markup_tool(Tool t) {  // markup: a tool button
    m_doc_view.set_tool(t);
    if (t != Tool::Select) {
        const auto k = kind_of(t);
        m_doc_view.set_pen(m_markup_color ? m_markup_color : M::default_color(k), 2);
    }
    sync_markup_bar();
}

void Shell::set_markup_color(std::uint32_t c) {  // markup: a swatch (recolours a selection)
    m_markup_color = c;
    m_doc_view.set_pen(c, 2);
    const auto sel = m_doc_view.selected_mark();
    if (m_editing && sel.valid()) {
        const auto id = m_pages.id_at(sel.page);
        if (auto rec = m_pages.get(id); rec && sel.index < int(rec->marks.size())) {
            auto m = rec->marks[std::size_t(sel.index)];
            if (m.kind != core::MarkKind::Signature && m.color != c) {
                m.color = c;
                P::set_mark(m_journal, m_pages, id, sel.index, m, "Recolour");
                m_doc_view.select_mark(sel.page, sel.index);
            }
        }
    }
    sync_markup_bar();
}

void Shell::sync_markup_bar() {  // markup: toggles follow the view's tool
    m_markup_syncing = true;
    for (auto& [t, b] : m_tool_buttons) b->set_active(t == m_doc_view.tool());
    // The swatch lit is the colour chosen; none lit = each tool's own colour.
    for (auto& [c, b] : m_swatches) b->set_active(c == m_markup_color);
    m_markup_syncing = false;
}

void Shell::on_mark_create(int page, core::Mark m) {  // markup: a new mark (notes / text: edit first)
    if (!m_editing) return;
    if (m.kind == core::MarkKind::Note || m.kind == core::MarkKind::Text) {
        m_doc_view.set_tool(Tool::Select);
        sync_markup_bar();
        edit_mark_text(page, -1, m);
        return;
    }
    const auto id = m_pages.id_at(page);
    if (!P::add_mark(m_journal, m_pages, id, m)) return;
    if (m.kind != core::MarkKind::Highlight) {
        m_doc_view.set_tool(Tool::Select);
        sync_markup_bar();
        m_doc_view.select_mark(page, int(m_pages.get(id)->marks.size()) - 1);
    }
}

void Shell::on_mark_edit(int page, int index, core::Mark m, std::string verb) {  // markup
    if (!m_editing) return;
    P::set_mark(m_journal, m_pages, m_pages.id_at(page), index, m, verb);
    m_doc_view.select_mark(page, index);
}

void Shell::on_mark_open(int page, int index) {  // markup: double-click a note / text
    const auto rec = m_pages.get(m_pages.id_at(page));
    if (!m_editing || !rec || index < 0 || index >= int(rec->marks.size())) return;
    edit_mark_text(page, index, rec->marks[std::size_t(index)]);
}

void Shell::edit_mark_text(int page, int index, core::Mark draft) {  // markup: the text popover
    m_text_edit = {page, index, draft, true};
    m_text_view->get_buffer()->set_text(draft.text);
    m_text_delete->set_visible(index >= 0);
    const auto r = index >= 0 ? m_doc_view.mark_screen_box(page, index) : m_doc_view.mark_screen_box(page, draft);
    Gdk::Rectangle at(int(r.x), int(r.y), std::max(1, int(r.w)), std::max(1, int(r.h)));
    m_text_pop->set_pointing_to(at);
    m_text_pop->popup();
    m_text_view->grab_focus();
}

// The editor closed (Done, Ctrl+Enter, Esc, a click away): what it says is
// kept. A new note / text with nothing in it is not made; an old one emptied
// is deleted. A text box grows to its text.
void Shell::commit_text() {  // markup: the popover closed
    if (!m_text_edit.open || !m_editing) return;
    m_text_edit.open = false;
    auto buf = m_text_view->get_buffer();
    std::string text = buf->get_text();
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    const int page = m_text_edit.page, index = m_text_edit.index;
    const auto id = m_pages.id_at(page);
    auto rec = m_pages.get(id);
    if (!rec) return;
    auto m = m_text_edit.draft;
    if (index >= 0) {
        if (index >= int(rec->marks.size())) return;
        m = rec->marks[std::size_t(index)];
    }
    if (text.empty()) {
        if (index >= 0) { m_doc_view.clear_mark(); P::remove_mark(m_journal, m_pages, id, index); }
        return;
    }
    if (index >= 0 && text == m.text) { m_doc_view.select_mark(page, index); return; }
    m.text = text;
    if (m.kind == core::MarkKind::Text) {
        const double th = mark_art::text_height(m);
        ((m.turn % 2) ? m.w : m.h) = th;
    }
    if (index >= 0) {
        P::set_mark(m_journal, m_pages, id, index, m, "Edit");
        m_doc_view.select_mark(page, index);
    } else if (P::add_mark(m_journal, m_pages, id, m)) {
        m_doc_view.select_mark(page, int(m_pages.get(id)->marks.size()) - 1);
    }
}

bool Shell::delete_selected_mark() {  // markup: Delete with a mark selected
    const auto sel = m_doc_view.selected_mark();
    if (!m_editing || !sel.valid()) return false;
    m_doc_view.clear_mark();
    return P::remove_mark(m_journal, m_pages, m_pages.id_at(sel.page), sel.index);
}

}  // namespace lookit
