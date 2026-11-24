// lookit_selftest -- the headless core harness.
//
// A seed is only trustworthy if its paradigms are EXERCISED, not just described
// (CANON: "a claim in the doc isn't a fact until a consumer exercises it"). This
// runs the GTK-free core with no display, so a future you can trust the pump and
// the mapping before wiring any widget on top:
//   * Recents  -- move-to-front + trim + the JSON round-trip with stale-prune;
//   * TextMap  -- forward(selection) -> inverse(map_range) round-trips byte-exact
//                 across single-line / multi-line / cross-paragraph / UTF-8, and
//                 chrome lines are never landed on.
//
//   cmake --build build --target lookit_selftest && ./build/lookit_selftest

#include "core/Drawers.hpp"
#include "core/Recents.hpp"
#include "core/Shortcuts.hpp"
#include "core/Stones.hpp"
#include "core/Undo.hpp"
#include "core/TextMap.hpp"
#include "core/TileText.hpp"
#include "core/Layout.hpp"
#include "core/FileKind.hpp"
#include <cmath>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace core = lookit::core;

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}

// The FORWARD mapping, mirroring what a viewer's selection() would do: a
// (al,ac)-(cl,cc) mark -> V byte endpoints -> codepoint range + quote. Returns
// the byte endpoints too, so the round-trip through map_range can be exact.
struct Fwd { int cp_start, cp_end, bstart, bend; std::string quote, V; };
Fwd forward(const std::vector<core::FlatLine>& flat, int al, int ac, int cl, int cc) {
    std::vector<int> off;
    std::string V = core::visible_text(flat, off);
    int bstart = off[static_cast<std::size_t>(al)] +
                 std::min(ac, static_cast<int>(flat[static_cast<std::size_t>(al)].text.size()));
    int bend   = off[static_cast<std::size_t>(cl)] +
                 std::min(cc, static_cast<int>(flat[static_cast<std::size_t>(cl)].text.size()));
    std::string quote = V.substr(static_cast<std::size_t>(bstart),
                                 static_cast<std::size_t>(bend - bstart));
    int cps = core::utf8_length(V.substr(0, static_cast<std::size_t>(bstart)));
    int cpe = cps + core::utf8_length(quote);
    return {cps, cpe, bstart, bend, quote, V};
}

// Assert map_range inverts forward(): endpoints mapped back through byte_off
// reproduce the forward byte endpoints and reconstruct the same quote.
void roundtrip(const std::vector<core::FlatLine>& flat,
               int al, int ac, int cl, int cc, const std::string& what) {
    Fwd f = forward(flat, al, ac, cl, cc);
    std::vector<int> off;
    (void)core::visible_text(flat, off);
    core::LineCol s, e;
    const bool ok = core::map_range(flat, f.cp_start, f.cp_end, s, e);
    check("textmap: " + what + " maps", ok);
    if (!ok) return;
    const int rb0 = off[static_cast<std::size_t>(s.line)] + s.col;
    const int rb1 = off[static_cast<std::size_t>(e.line)] + e.col;
    check("textmap: " + what + " round-trips byte-exact",
          rb0 == f.bstart && rb1 == f.bend,
          std::to_string(rb0) + "," + std::to_string(rb1) +
          " vs " + std::to_string(f.bstart) + "," + std::to_string(f.bend));
    check("textmap: " + what + " reconstructs quote",
          f.V.substr(static_cast<std::size_t>(rb0),
                     static_cast<std::size_t>(rb1 - rb0)) == f.quote);
}
}  // namespace

int main() {
    std::cout << "lookit_selftest -- recents pump + textmap mapping\n";

    // ── Undo -- the one door, two pictures, gestures, merging (s046) ──────────
    {
        std::cout << "\n-- undo --\n";
        using core::Stones;
        using core::Stone;
        using Clock = core::StoneJournal::Clock;
        using SnapMap = std::vector<std::pair<std::string, Stone>>;
        const auto snap = [](const Stones& s) {
            SnapMap out;
            for (const auto& id : s.ids()) out.emplace_back(id, *s.get(id));
            return out;
        };
        const auto names = [](const Stones& s) {
            std::string out;
            for (const auto& id : s.ids()) out += s.get(id)->name + ",";
            return out;
        };
        // A fixture: three stones, LOADED (not a user choice -- not undoable).
        struct Fix {
            Stones s;
            core::StoneJournal j{s};
            std::vector<std::string> sel;
            Fix() {
                s.set_door([this] { return j.open(); });
                j.set_selection_source([this] { return sel; });
                Stones::Loading load(s);
                for (const char* n : {"Ruby", "Jade", "Onyx"}) {
                    const auto id = s.fresh_id();
                    s.put(id, Stone{n, 3, s.pos_after({})});
                }
            }
        };

        // THE GATE. Every verb the page offers, from a fresh fixture: nothing
        // written with the door shut; at least one step; undoing them all puts
        // the model back exactly; redoing them all reproduces the result.
        for (const auto& v : core::stones::verbs()) {
            Fix f;
            const auto first = f.s.ids().front();
            const auto before = snap(f.s);
            v.run(f.j, f.s, first);
            const auto after = snap(f.s);
            const auto steps = f.j.size();
            check("gate/" + v.name + ": through the one door", f.s.strays() == 0,
                  std::to_string(f.s.strays()) + " stray write(s)");
            check("gate/" + v.name + ": is a step", steps > 0 && !(after == before));
            for (std::size_t i = 0; i < steps; ++i) f.j.undo();
            check("gate/" + v.name + ": undo puts it back exactly", snap(f.s) == before);
            for (std::size_t i = 0; i < steps; ++i) f.j.redo();
            check("gate/" + v.name + ": redo reproduces it", snap(f.s) == after);
        }
        {   // ...and the gate bites: a write around the door is counted.
            Fix f;
            const auto id = f.s.ids().front();
            f.s.put(id, Stone{"sneaky", 3, 0.5});
            check("gate: a write around the door is a stray", f.s.strays() == 1);
        }

        {   // Delete comes back whole, in its place, under its id, selected.
            Fix f;
            const auto ids = f.s.ids();
            f.sel = {ids[1]};
            core::stones::remove(f.j, f.s, ids[1]);
            f.sel = {ids[2]};      // the UI selects the neighbour...
            f.j.reselect(f.sel);   // ...and tells the step
            check("delete: gone", names(f.s) == "Ruby,Onyx,", names(f.s));
            check("delete: the label names it", f.j.undo_label() == "Delete “Jade”", f.j.undo_label());
            const auto r = f.j.undo();
            check("delete: undo -- back in its place", names(f.s) == "Ruby,Jade,Onyx,", names(f.s));
            check("delete: undo -- the same id", f.s.ids()[1] == ids[1]);
            check("delete: undo hands back its selection", r.selection == std::vector<std::string>{ids[1]});
            const auto r2 = f.j.redo();
            check("delete: redo -- gone again, the neighbour selected",
                  names(f.s) == "Ruby,Onyx," && r2.selection == std::vector<std::string>{ids[2]});
        }

        {   // Created records: undo removes, redo brings back the same id.
            Fix f;
            const auto first = f.s.ids()[0];
            const auto made = core::stones::add(f.j, f.s, "Opal", first);
            check("add: just after the one given", names(f.s) == "Ruby,Opal,Jade,Onyx,", names(f.s));
            f.j.undo();
            check("add: undone", !f.s.get(made));
            f.j.redo();
            check("add: redone, same id, same place", f.s.get(made) && names(f.s) == "Ruby,Opal,Jade,Onyx,");
        }

        {   // A no-op is not a step.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::rename(f.j, f.s, id, "Ruby");
            core::stones::remove(f.j, f.s, "nope");
            core::stones::move_up(f.j, f.s, id);   // already first
            check("no-op: nothing kept", f.j.size() == 0 && !f.j.can_undo());
        }

        {   // Merging: arrows on the slider fold into one step inside the window.
            Fix f;
            const auto id = f.s.ids()[0];
            const auto t0 = Clock::now();
            core::stones::resize(f.j, f.s, id, 4);
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 5; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(400));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 6; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::milliseconds(800));
            check("merge: three presses, one step", f.j.size() == 1, std::to_string(f.j.size()));
            f.j.run("Size", {id}, [&] { auto v = *f.s.get(id); v.size = 7; f.s.put(id, v); }, "size:" + id,
                    t0 + std::chrono::seconds(10));
            check("merge: after the window, a new step", f.j.size() == 2);
            f.j.undo();
            f.j.undo();
            check("merge: two undos, back to the start", f.s.get(id)->size == 3);
            // Another stone's key never merges into this one's step.
            Fix g;
            const auto a = g.s.ids()[0], b = g.s.ids()[1];
            core::stones::resize(g.j, g.s, a, 5);
            core::stones::resize(g.j, g.s, b, 5);
            check("merge: a different key is a different step", g.j.size() == 2);
            // Nor into a step that was undone.
            core::stones::resize(g.j, g.s, b, 6);
            g.j.undo();
            core::stones::resize(g.j, g.s, b, 7);
            check("merge: never into an undone step", g.j.size() == 2 && g.j.can_undo() && !g.j.can_redo());
            // Back to where it started inside the window: the step goes away.
            Fix h;
            const auto c = h.s.ids()[0];
            core::stones::resize(h.j, h.s, c, 4);
            core::stones::resize(h.j, h.s, c, 3);
            check("merge: there and back again is no step", h.j.size() == 0);
        }

        {   // A gesture is one step, however many writes the drag made.
            Fix f;
            const auto id = f.s.ids()[0];
            core::stones::drag_begin(f.j, f.s, id);
            for (int n = 4; n <= 9; ++n) core::stones::drag_to(f.j, f.s, id, n);
            check("gesture: open while dragging", f.j.in_gesture() && f.j.size() == 0);
            core::stones::drag_end(f.j);
            check("gesture: a six-write drag is one step", f.j.size() == 1 && f.s.get(id)->size == 9);
            f.j.undo();
            check("gesture: one undo, back to the start", f.s.get(id)->size == 3);
            // Escape mid-drag: back, and nothing kept.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 7);
            f.j.cancel();
            check("gesture: cancel puts it back and keeps nothing",
                  f.s.get(id)->size == 3 && !f.j.in_gesture() && f.j.can_redo());
            // Ctrl+Z mid-drag: the drag becomes a step, then is undone.
            core::stones::drag_begin(f.j, f.s, id);
            core::stones::drag_to(f.j, f.s, id, 2);
            f.j.undo();
            check("gesture: undo mid-drag closes the drag, then undoes it",
                  !f.j.in_gesture() && f.s.get(id)->size == 3 && f.j.can_redo());
            check("gesture: no stray writes", f.s.strays() == 0);
        }

        {   // A new step forgets the redo branch; Move keeps both stones in one step.
            Fix f;
            const auto ids = f.s.ids();
            core::stones::move_down(f.j, f.s, ids[0]);
            check("move: one step for both stones", f.j.size() == 1 && names(f.s) == "Jade,Ruby,Onyx,", names(f.s));
            f.j.undo();
            check("move: undone", names(f.s) == "Ruby,Jade,Onyx,");
            core::stones::rename(f.j, f.s, ids[2], "Coal");
            check("redo branch: a new step forgets it", !f.j.can_redo() && f.j.size() == 1);
        }

        {   // The cap: the oldest step falls off; the newest still undoes.
            Fix f;
            const auto id = f.s.ids()[0];
            for (std::size_t i = 0; i < core::StoneJournal::kMax + 20; ++i)
                core::stones::rename(f.j, f.s, id, "n" + std::to_string(i));
            check("cap: kMax steps kept", f.j.size() == core::StoneJournal::kMax);
            f.j.undo();
            check("cap: the newest still undoes", f.s.get(id)->name == "n" + std::to_string(core::StoneJournal::kMax + 18));
        }

        {   // on_change: the UI hears every journal change (labels, greying).
            Fix f;
            int heard = 0;
            f.j.set_on_change([&] { ++heard; });
            core::stones::rename(f.j, f.s, f.s.ids()[0], "X");
            f.j.undo();
            f.j.redo();
            f.j.clear();
            check("on_change: step, undo, redo, clear -- four", heard == 4, std::to_string(heard));
        }
    }

    // ── Drawers: at most one open, the scrim iff one is open ─────────────────
    {
        std::cout << "[drawers]\n";
        using Side = core::Drawers::Side;
        core::Drawers d;
        check("drawers: start closed, no scrim", d.open() == Side::None && !d.scrim());
        check("drawers: close when closed changes nothing", !d.close());
        check("drawers: toggle left opens it", d.toggle(Side::Left) && d.is_open(Side::Left) && d.scrim());
        check("drawers: toggle right closes left, opens right",
              d.toggle(Side::Right) && d.is_open(Side::Right) && !d.is_open(Side::Left));
        check("drawers: show the open one changes nothing", !d.show(Side::Right));
        check("drawers: toggle the open one closes it", d.toggle(Side::Right) && !d.scrim());
        d.show(Side::Left);
        check("drawers: close closes whichever is open", d.close() && d.open() == Side::None);
        check("drawers: None is never 'open'", !d.is_open(Side::None));
        // The keys the drawers advertise are wired: both toggles in the registry.
        int wired = 0;
        for (const auto& r : core::shortcut_registry())
            if (r.action == "win.toggle-left-drawer" || r.action == "win.toggle-right-drawer") ++wired;
        check("drawers: both toggles have a registered key", wired == 2);
    }

    std::cout << "-----------------------------------------------\n";

    // -- Recents: list ops + JSON round-trip with prune -----------------------
    {
        std::vector<std::string> l;
        core::recents_add(l, "/a");
        core::recents_add(l, "/b");
        core::recents_add(l, "/c");
        check("recents: newest first", l.size() == 3 && l.front() == "/c");
        core::recents_add(l, "/a");
        check("recents: re-add moves to front (no dup)",
              l.size() == 3 && l.front() == "/a");
        core::recents_add(l, "/x/");
        check("recents: trailing slash normalised",
              std::find(l.begin(), l.end(), "/x") != l.end() &&
              std::find(l.begin(), l.end(), "/x/") == l.end());
        std::vector<std::string> cap;
        for (int i = 0; i < 20; ++i)
            core::recents_add(cap, "/p" + std::to_string(i), 8);
        check("recents: trim to max (8)", cap.size() == 8 && cap.front() == "/p19");

        const std::string base =
            (std::filesystem::temp_directory_path() / "lookit_selftest_recents").string();
        std::error_code ec;
        std::filesystem::remove_all(base, ec);
        const std::string live = base + "/live";
        std::filesystem::create_directories(live, ec);
        const std::string file = base + "/recent.json";

        std::vector<std::string> to_save = {live, base + "/GONE"};
        check("recents: save ok", core::save_recents(file, to_save));
        auto loaded = core::load_recents(file);
        check("recents: load prunes the missing path",
              loaded.size() == 1 && loaded.front() == live);
        check("recents: missing file -> empty",
              core::load_recents(base + "/nope.json").empty());
    }

    // -- TextMap: forward/inverse round-trip ----------------------------------
    {
        // A scene as a viewer flattens it:
        //   0 "The Cellar"     title  (non-prose)
        //   1 ""               gap    (non-prose)
        //   2 "She opened the" prose, para_start   <- para 1, wrap 1
        //   3 "old iron door." prose               <- para 1, wrap 2
        //   4 "Caf\u00e9 dust." prose, para_start  <- para 2 (UTF-8: é)
        std::vector<core::FlatLine> flat = {
            {"The Cellar",     false, false},
            {"",               false, false},
            {"She opened the", true,  true },
            {"old iron door.", true,  false},
            {"Caf\xC3\xA9 dust.", true, true },
        };
        roundtrip(flat, 2, 0, 2, 14, "single line");
        roundtrip(flat, 2, 4, 3, 8,  "multi-line (soft wrap)");
        roundtrip(flat, 2, 0, 4, 10, "cross-paragraph");
        roundtrip(flat, 4, 0, 4, 6,  "UTF-8 span (accented)");

        // Chrome is never landed on: a range covering all prose starts on line 2.
        std::vector<int> off;
        (void)core::visible_text(flat, off);
        core::LineCol s, e;
        core::map_range(flat, 0, 3, s, e);
        check("textmap: start snaps to prose, never chrome", s.line == 2);

        // Degenerate / empty inputs fail cleanly.
        check("textmap: empty range rejected", !core::map_range(flat, 5, 5, s, e));
        std::vector<core::FlatLine> no_prose = {{"Title", false, false}};
        check("textmap: no prose -> false", !core::map_range(no_prose, 0, 3, s, e));
    }
    // -- Shortcuts: the registry paradigm, exercised ---------------------------
    // A seed's claim isn't a fact until a consumer exercises it. The registry's
    // whole promise is that a key and its advertised row can't drift, because
    // both read one list -- so the guard that matters is pure and lives here:
    // no two chords may collide. (App wires from this same list; ShortcutsDialog
    // renders from it.)
    {
        namespace sc = lookit::core;

        check("shortcuts: format_accel renders modifiers + key",
              sc::format_accel("<Ctrl><Shift>n") == "Ctrl+Shift+N",
              sc::format_accel("<Ctrl><Shift>n"));
        check("shortcuts: format_accel maps a named key to its glyph",
              sc::format_accel("<Ctrl>question") == "Ctrl+?",
              sc::format_accel("<Ctrl>question"));

        {
            sc::ShortcutSpec derived; derived.accels = {"<Ctrl>question", "<Ctrl>slash"};
            check("shortcuts: display_keys derives + joins accels",
                  derived.display_keys() == "Ctrl+?  /  Ctrl+/",
                  derived.display_keys());
            sc::ShortcutSpec doc; doc.keys = "Click a recent";
            check("shortcuts: display_keys prefers an explicit key string",
                  doc.display_keys() == "Click a recent");
        }

        check("shortcuts: registry is populated",
              !sc::shortcut_registry().empty(),
              std::to_string(sc::shortcut_registry().size()) + " specs");
        check("shortcuts: registry is accel-collision-free",
              sc::find_accel_collisions().empty(),
              sc::find_accel_collisions().empty()
                  ? "clean" : sc::find_accel_collisions().front());

        // Sections authored A-Z + contiguous: the dialog walks linearly and
        // starts a heading on change, so a stray out-of-order row would split a
        // section into two headings.
        {
            std::vector<std::string> secs;
            for (const auto& r : sc::shortcut_registry())
                if (secs.empty() || secs.back() != r.section) secs.push_back(r.section);
            std::vector<std::string> sorted = secs;
            std::sort(sorted.begin(), sorted.end());
            check("shortcuts: sections are contiguous and A-Z ordered", secs == sorted);
        }

        // The doc-only half of the paradigm: a gesture row documents without
        // binding (no accels), but must still show keys.
        {
            bool ok = true, saw_doc_only = false;
            for (const auto& r : sc::shortcut_registry())
                if (r.action.empty()) {
                    saw_doc_only = true;
                    if (!r.accels.empty() || r.keys.empty()) ok = false;
                }
            check("shortcuts: a doc-only row binds nothing but still displays keys",
                  ok && saw_doc_only);
        }
    }


    // ── The quick-settings-tile stone: the app side of what the tile sends ──
    // The SAME cases tools/test_extension_lib.mjs runs against lib.js, so the
    // shell's cleaning and the app's can't drift apart unnoticed.
    {
        namespace tile = core::tile;
        using W = tile::Where;
        check("tile: CRLF -> LF, trailing blanks off, ends trimmed",
              tile::cleaned("  One.  \r\nTwo. \t\r\n") == "One.\nTwo.");
        check("tile: blank runs squeezed to one blank line",
              tile::cleaned("A.\n\n\n\nB.") == "A.\n\nB.");
        check("tile: blanks inside a line are kept",
              tile::cleaned("a  b\tc") == "a  b\tc");
        check("tile: lone CR is a line break",
              tile::cleaned("a\rb") == "a\nb");
        {
            auto r = tile::request("   ", W::Line);
            check("tile: empty line -> nothing, no error", r.text.empty() && r.error.empty());
        }
        check("tile: empty clipboard / selection -> its own error",
              tile::request("", W::Clipboard).error == "The clipboard has no text." &&
              tile::request("\n", W::Selection).error == "Nothing is selected.");
        check("tile: text -> cleaned text",
              tile::request(" The ball was thrown by Sam. ").text == "The ball was thrown by Sam.");
        {
            auto over = tile::request(std::string(tile::kMaxChars + 1, 'x'));
            check("tile: too long -> error, nothing taken", over.text.empty() && !over.error.empty());
            check("tile: exactly the cap is fine",
                  tile::request(std::string(tile::kMaxChars, 'x')).text.size() == tile::kMaxChars);
        }
        check("tile: the cap counts characters, not bytes",
              tile::codepoints("\xc3\xa9t\xc3\xa9") == 3);   // "été"
    }


    // ── Lookit: the page layout + the zoom ladder ───────────────────────────
    {
        namespace L = core::layout;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        const std::vector<L::Size> letter3(3, L::Size{612, 792});
        auto p = L::place(letter3, 1.0, 1000);
        check("layout: content width = viewport when pages are narrower", near(p.width, 1000));
        check("layout: pages centred", near(p.pages[0].x, std::floor((1000 - 612) / 2.0)));
        check("layout: first page a margin down", near(p.pages[0].y, L::kMargin));
        check("layout: pages a gap apart", near(p.pages[1].y, p.pages[0].y + 792 + L::kGap));
        check("layout: height = margins + pages + gaps",
              near(p.height, 2 * L::kMargin + 3 * 792 + 2 * L::kGap));
        auto wide = L::place(letter3, 2.0, 800);
        check("layout: content widens past the viewport when zoomed", near(wide.width, 1224 + 2 * L::kMargin));
        check("layout: a wide page starts at the margin", near(wide.pages[0].x, L::kMargin));
        auto v = L::visible(p, 0, 500);
        check("layout: top of the view shows page 1 only", v.first == 0 && v.second == 0);
        v = L::visible(p, 800, 900);
        check("layout: a gap straddle shows both neighbours", v.first == 0 && v.second == 1);
        v = L::visible(p, 1e6, 1e6 + 100);
        check("layout: past the end shows nothing", v.first == -1);
        check("layout: nothing visible in an empty doc", L::visible(L::place({}, 1, 800), 0, 100).first == -1);
        check("layout: page_at the middle of page 2", L::page_at(p, p.pages[1].y + 400) == 1);
        check("layout: page_at in a gap picks the nearer page",
              L::page_at(p, p.pages[0].y + 792 + 2) == 0 && L::page_at(p, p.pages[1].y - 2) == 1);
        check("layout: page_at in an empty doc is -1", L::page_at(L::place({}, 1, 800), 10) == -1);
        check("layout: fit width fills the viewport less margins",
              near(L::fit_width(letter3, 660), (660 - 2 * L::kMargin) / 612.0));
        {
            const std::vector<L::Size> mixed{{612, 792}, {1224, 792}};
            check("layout: fit width follows the widest page",
                  near(L::fit_width(mixed, 1272), (1272 - 2 * L::kMargin) / 1224.0));
        }
        check("layout: fit page takes the tighter side",
              near(L::fit_page(letter3, 0, 2000, 840), (840 - 2 * L::kMargin) / 792.0));
        check("layout: fit with no room is 100%, not zero or negative", near(L::fit_width(letter3, 10), 1.0));
        check("layout: zoom in from 100% -> 125%", near(L::zoom_in(1.0), 1.25));
        check("layout: zoom out from 100% -> 90%", near(L::zoom_out(1.0), 0.9));
        check("layout: zoom in from between steps goes to the next step", near(L::zoom_in(1.1), 1.25));
        check("layout: the ladder stops at its ends",
              near(L::zoom_in(8.0), 8.0) && near(L::zoom_out(0.25), 0.25));
        check("layout: clamp keeps scale sane",
              near(L::clamp_scale(100), L::kMaxScale) && near(L::clamp_scale(0), 1.0) &&
              near(L::clamp_scale(-3), 1.0));
    }


    // ── Lookit: Book view -- spreads like a printed book ────────────────────
    {
        namespace L = core::layout;
        using M = L::Mode;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        auto r5 = L::rows(5, M::Book);
        check("book: page 1 alone on the right, then 2|3, 4|5",
              r5.size() == 3 && r5[0].left == -1 && r5[0].right == 0 && r5[1].left == 1 &&
              r5[1].right == 2 && r5[2].left == 3 && r5[2].right == 4);
        auto r4 = L::rows(4, M::Book);
        check("book: an even last page sits alone on the left",
              r4.size() == 3 && r4[2].left == 3 && r4[2].right == -1);
        check("book: rows of a one-page doc", L::rows(1, M::Book).size() == 1);
        check("book: row_of / first_of_row",
              L::row_of(0, M::Book) == 0 && L::row_of(1, M::Book) == 1 && L::row_of(2, M::Book) == 1 &&
              L::row_of(3, M::Book) == 2 && L::first_of_row(2, M::Book) == 3 && L::first_of_row(0, M::Book) == 0);
        check("book: step goes spread to spread",
              L::step(0, +1, 9, M::Book) == 1 && L::step(1, +1, 9, M::Book) == 3 &&
              L::step(2, +1, 9, M::Book) == 3 && L::step(3, -1, 9, M::Book) == 1 &&
              L::step(1, -1, 9, M::Book) == 0);
        check("book: step stops at the ends",
              L::step(0, -1, 9, M::Book) == 0 && L::step(8, +1, 9, M::Book) == 7);
        check("continuous: step is one page", L::step(4, +1, 9, M::Continuous) == 5 &&
                                              L::step(8, +1, 9, M::Continuous) == 8);
        const std::vector<L::Size> pg(5, L::Size{600, 800});
        auto p = L::place(pg, 1.0, 1000, M::Book);
        const double mid = std::floor(p.width / 2);
        check("book: the cover starts at the spine, on the right",
              near(p.pages[0].x, mid + L::kSpine / 2));
        check("book: a spread meets at the spine",
              near(p.pages[1].x + p.pages[1].w, mid - L::kSpine / 2) && near(p.pages[2].x, mid + L::kSpine / 2));
        check("book: facing pages share a row", near(p.pages[1].y, p.pages[2].y) && near(p.pages[3].y, p.pages[4].y));
        check("book: three rows tall", near(p.height, 2 * L::kMargin + 3 * 800 + 2 * L::kGap));
        check("book: widens to fit a spread", near(p.width, 1200 + L::kSpine + 2 * L::kMargin));
        auto v = L::visible(p, p.pages[1].y + 10, p.pages[1].y + 20);
        check("book: a spread shows both its pages", v.first == 1 && v.second == 2);
        check("book: fit width fits the spread",
              near(L::fit_width(pg, 1250, M::Book), (1250 - 2 * L::kMargin - L::kSpine) / 1200.0));
        check("book: fit width treats the lone cover as half a spread (same scale)",
              near(L::fit_width(std::vector<L::Size>(1, L::Size{600, 800}), 1250, M::Book),
                   L::fit_width(std::vector<L::Size>(1, L::Size{600, 800}), 1250, M::Continuous)));
        {
            std::vector<L::Size> uneven{{600, 800}, {600, 800}, {600, 500}};
            auto q = L::place(uneven, 1.0, 1300, M::Book);
            check("book: a shorter facing page is centred on the row",
                  near(q.pages[2].y, q.pages[1].y + 150));
            check("book: page_at finds the taller page when the y misses the shorter one",
                  L::page_at(q, q.pages[1].y + 20) == 1);
        }
    }

    // ── Lookit: what a file is ──────────────────────────────────────────────
    {
        using K = core::Kind;
        check("kind: %PDF magic", core::kind_from_magic("%PDF-1.7\n") == K::Pdf);
        check("kind: PNG / JPEG / GIF / TIFF magic",
              core::kind_from_magic("\x89PNG\r\n\x1a\n") == K::Image &&
              core::kind_from_magic("\xFF\xD8\xFF\xE0") == K::Image &&
              core::kind_from_magic("GIF89a") == K::Image &&
              core::kind_from_magic(std::string("II*\0", 4)) == K::Image);
        check("kind: WEBP and HEIC magic",
              core::kind_from_magic(std::string("RIFF\x10\0\0\0WEBPVP8 ", 16)) == K::Image &&
              core::kind_from_magic(std::string("\0\0\0\x18" "ftypheic", 12)) == K::Image);
        check("kind: DjVu magic", core::kind_from_magic("AT&TFORM\0\0") == K::DjVu);
        {
            std::string epub = "PK\x03\x04";
            epub.resize(30, '\0');
            epub += "mimetypeapplication/epub+zip";
            check("kind: EPUB's stored mimetype", core::kind_from_magic(epub) == K::Epub);
        }
        check("kind: by name, case-blind",
              core::kind_from_name("/a/B.PDF") == K::Pdf && core::kind_from_name("x.JpEg") == K::Image &&
              core::kind_from_name("c.cbz") == K::Cbz && core::kind_from_name("d.djvu") == K::DjVu);
        check("kind: a dot in a folder name is not an extension",
              core::kind_from_name("/a.pdf/readme") == K::Unknown);
        {
            const auto dir = std::filesystem::temp_directory_path() / "lookit_selftest_kind";
            std::filesystem::create_directories(dir);
            const auto liar = (dir / "really_a_png.pdf").string();
            { std::ofstream(liar, std::ios::binary) << "\x89PNG\r\n\x1a\n rest"; }
            check("kind: the bytes beat the name (a .pdf that is a PNG)", core::detect(liar) == K::Image);
            const auto fake_cbz = (dir / "not_a_zip.cbz").string();
            { std::ofstream(fake_cbz) << "hello"; }
            check("kind: a .cbz that isn't a zip is Unknown", core::detect(fake_cbz) == K::Unknown);
            check("kind: a missing file falls back to its name",
                  core::detect((dir / "missing.pdf").string()) == K::Pdf);
            std::filesystem::remove_all(dir);
        }
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
