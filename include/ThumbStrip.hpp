#pragma once
#include "Document.hpp"

#include <gtkmm/listview.h>
#include <gtkmm/picture.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/singleselection.h>
#include <gtkmm/stringlist.h>
#include <sigc++/signal.h>

#include <deque>
#include <map>
#include <thread>
#include <utility>
#include <memory>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// ThumbStrip -- the page thumbnails down the left of View. A ListView, so only
// the rows on screen exist and a thumbnail is drawn the first time its row is
// bound (then cached) -- a 900-page PDF costs what you scroll past, not 900
// renders up front. Clicking one asks the view to go there (signal_pick); the
// view's page comes back through set_current, which selects and scrolls to it
// without echoing a pick. Thumbnails are drawn on a thread of their own from
// the document's twin (s014.2), so a slow page never stalls the window.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit {

class ThumbStrip : public Gtk::ScrolledWindow {
public:
    ThumbStrip();
    ~ThumbStrip() override;
    struct Job;   // the thumbnail thread's share (ThumbStrip.cpp)

    void set_document(std::shared_ptr<Document> doc);
    void set_current(int page);
    sigc::signal<void(int)>& signal_pick() { return m_pick; }
    // Drag-and-drop (when editable): a thumbnail dragged to a new place, and
    // files dropped between thumbnails -- `to` is the position to insert before.
    void set_editable(bool on) { m_editable = on; }
    sigc::signal<void(int, int)>& signal_move() { return m_move; }
    sigc::signal<void(std::vector<std::string>, int)>& signal_files() { return m_files; }

private:
    Glib::RefPtr<Gdk::Texture> thumb(int page);          // drawn here, now
    void keep(int page, const Glib::RefPtr<Gdk::Texture>& t);   // into the cache
    void request(int page);   // draw it soon (thread, else idle)
    void forget(int page);    // its row went away: don't bother
    int focus() const;        // the page at the middle of the strip now
    void deliver(int page, const Glib::RefPtr<Gdk::Texture>& t);
    static void show(Gtk::Picture* pic, const Glib::RefPtr<Gdk::Texture>& t);
    void drop_job();
    static void run(std::shared_ptr<Job> job, int width);

    static constexpr int kWidth = 112;   // thumbnail width in logical px

    Gtk::ListView m_list;
    Glib::RefPtr<Gtk::StringList> m_pages;
    Glib::RefPtr<Gtk::SingleSelection> m_sel;
    std::shared_ptr<Document> m_doc;
    std::map<int, Glib::RefPtr<Gdk::Texture>> m_cache;
    std::map<int, Gtk::Picture*> m_bound;   // page -> the row showing it now
    std::shared_ptr<Job> m_job;             // this document's thread, if any
    std::vector<std::pair<std::shared_ptr<Job>, std::thread>> m_threads;
    std::deque<int> m_idle_want;            // no twin: drawn on idle instead
    sigc::connection m_idle;
    bool m_syncing = false;
    sigc::signal<void(int)> m_pick;
    sigc::signal<void(int, int)> m_move;
    sigc::signal<void(std::vector<std::string>, int)> m_files;
    bool m_editable = false;
};

}  // namespace lookit
