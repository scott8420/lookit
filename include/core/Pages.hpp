#pragma once
#include "core/Marks.hpp"
#include "core/Undo.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Pages -- the page list being edited, in the journal's store shape.
//
// A document being edited is a list of page RECORDS: which source file, which
// page of it, how it's turned, and where it sits. Rotate / delete / move /
// insert change records; nothing touches a PDF until Save writes the list out
// (qpdf, Save.cpp). Every verb goes through the ONE door (Journal::run), so
// each is one undo step; the store counts writes made outside it and the
// selftest fails on a stray (Cairn's gate -- RULES "Undo from the first write").
//
// Sources only grow (an insert adds one); undo removes the pages, never the
// source, so a redo can put them back.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

struct PageRec {
    int    src = 0;        // index into Pages::sources()
    int    index = 0;      // page number in that source (0-based)
    int    turn = 0;       // quarter turns clockwise, 0..3
    double pos = 0;        // place in the list (ascending)
    std::vector<Mark> marks;     // signatures and markup on this page, bottom to top (core/Marks)
    bool operator==(const PageRec&) const = default;
};

class Pages {
public:
    using Id    = int;
    using Value = PageRec;

    // ── the journal's contract ──
    std::optional<PageRec> get(const Id& id) const;
    void put(const Id& id, const std::optional<PageRec>& v);

    // ── reading ──
    std::vector<Id> ids() const;                       // in order
    std::vector<PageRec> records() const;              // in order
    int  count() const { return int(m_items.size()); }
    Id   id_at(int position) const;                    // -1 when out of range
    int  position_of(Id id) const;                     // -1 when absent
    Id   fresh_id() { return m_next++; }
    const std::vector<std::string>& sources() const { return m_sources; }
    int  add_source(const std::string& path);          // not a page edit: no step

    // ── load (not a user choice, not undoable) ──
    void load(const std::string& path, int page_count);
    void clear();

    // ── the one-door guard ──
    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }
    void set_on_change(std::function<void()> f) { m_changed = std::move(f); }

private:
    std::map<Id, PageRec>    m_items;
    std::vector<std::string> m_sources;
    Id                       m_next = 1;
    int                      m_strays = 0;
    int                      m_loading = 0;
    std::function<bool()>    m_door;
    std::function<void()>    m_changed;
};

using PageJournal = Journal<Pages>;

// The verbs. Each is one step; each returns false (and records nothing) when it
// would change nothing or is refused.
namespace pages {
bool rotate(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids, int quarter_turns);
bool remove(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids);   // never the last page
bool move(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids, int to_position);
// Insert pages [0, count) of source `src` before `position` (== count: append).
std::vector<Pages::Id> insert(PageJournal& j, Pages& p, int src, int count, int position,
                              const std::string& what);
bool duplicate(PageJournal& j, Pages& p, Pages::Id id);

// Marks (core/Marks): signatures and markup. A mark belongs to its page's
// record, so each of these is one step on that one record. `index` counts the
// page's marks. Labels name the kind: "Add highlight", "Delete note".
bool add_mark(PageJournal& j, Pages& p, Pages::Id page, const Mark& m);
// `verb` names the change: "Move", "Resize", "Edit", "Recolour" -> "Move arrow".
bool set_mark(PageJournal& j, Pages& p, Pages::Id page, int index, const Mark& m, const std::string& verb);
bool remove_mark(PageJournal& j, Pages& p, Pages::Id page, int index);

// "3-7, 9, 12-" -> 0-based positions, in order, no repeats, within [0, count).
// Empty -> {}. A bad token -> {} with `error` set.
std::vector<int> parse_ranges(const std::string& text, int count, std::string& error);
}  // namespace pages

}  // namespace lookit::core
