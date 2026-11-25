#pragma once
#include "Document.hpp"

#include <gtkmm/listview.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/stringlist.h>
#include <sigc++/signal.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ThumbStrip -- the page thumbnails down the left of View. A ListView, so only
// the rows on screen exist and a thumbnail is drawn the first time its row is
// bound (then cached) -- a 900-page PDF costs what you scroll past, not 900
// renders up front. Clicking one asks the view to go there (signal_pick); the
// view's page comes back through set_current, which selects and scrolls to it
// without echoing a pick.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class ThumbStrip : public Gtk::ScrolledWindow {
public:
    ThumbStrip();

    void set_document(std::shared_ptr<Document> doc);
    void set_current(int page);
    sigc::signal<void(int)>& signal_pick() { return m_pick; }
    // Drag-and-drop (when editable): a thumbnail dragged to a new place, and
    // files dropped between thumbnails -- `to` is the position to insert before.
    void set_editable(bool on) { m_editable = on; }
    sigc::signal<void(int, int)>& signal_move() { return m_move; }
    sigc::signal<void(std::vector<std::string>, int)>& signal_files() { return m_files; }

private:
    Glib::RefPtr<Gdk::Texture> thumb(int page);

    static constexpr int kWidth = 112;   // thumbnail width in logical px

    Gtk::ListView m_list;
    Glib::RefPtr<Gtk::StringList> m_pages;
    Glib::RefPtr<Gtk::SingleSelection> m_sel;
    std::shared_ptr<Document> m_doc;
    std::map<int, Glib::RefPtr<Gdk::Texture>> m_cache;
    bool m_syncing = false;
    sigc::signal<void(int)> m_pick;
    sigc::signal<void(int, int)> m_move;
    sigc::signal<void(std::vector<std::string>, int)> m_files;
    bool m_editable = false;
};

}  // namespace lookit
