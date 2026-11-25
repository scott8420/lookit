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
#include "core/Search.hpp"
#include "core/Pages.hpp"
#include "core/Sign.hpp"
#include "core/Marks.hpp"
#include "core/Picture.hpp"
#include "core/Forms.hpp"
#include "core/Book.hpp"
#include "core/Zip.hpp"
#include "core/Forms.hpp"
#include "core/Tint.hpp"
#include "Css.hpp"   // plain text, no GTK: the skin the tint turns
#include <regex>
#include <set>
#include <cstdint>
#include <cmath>
#include <chrono>

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
        {
            const std::string longname =
                "/pics/Adolf_Hir\xC3\xA9my-Hirschl_-_Die_Seelen_am_Acheron_-_942_-_\xC3\x96sterreichische_Galerie_Belvedere.jpg";
            const std::string lab = core::recent_menu_label(longname);
            std::string plain;   // undo the mnemonic escape, count code points
            for (std::size_t i = 0; i < lab.size(); ++i) {
                plain += lab[i];
                if (lab[i] == '_' && i + 1 < lab.size() && lab[i + 1] == '_') ++i;
            }
            std::size_t cps = 0;
            for (unsigned char b : plain) if ((b & 0xC0) != 0x80) ++cps;
            check("recents: menu label cut to 36 characters", cps == core::kRecentLabelMax,
                  std::to_string(cps) + " " + lab);
            check("recents: menu label keeps the extension", plain.size() > 4 && plain.substr(plain.size() - 4) == ".jpg", lab);
            check("recents: menu label has the ellipsis", lab.find("\xE2\x80\xA6") != std::string::npos, lab);
            check("recents: menu label doubles every underscore",
                  core::recent_menu_label("/a/x_y.pdf") == "x__y.pdf", core::recent_menu_label("/a/x_y.pdf"));
            check("recents: short name unchanged", core::recent_menu_label("/a/b/report.pdf") == "report.pdf");
            check("recents: never splits a UTF-8 character",
                  core::recent_menu_label("/\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96.pdf", 8)
                      == "\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xE2\x80\xA6" "df",
                  core::recent_menu_label("/\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96\xC3\x96.pdf", 8));
        }
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



    // ── Lookit: paged view -- one page (or facing pair) at a time ───────────
    {
        namespace L = core::layout;
        using M = L::Mode;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        const std::vector<L::Size> pg(6, L::Size{600, 800});
        auto p = L::place_row(pg, 2, 0.5, 1000, 700);
        check("paged: only the current page is placed",
              near(p.pages[2].w, 300) && near(p.pages[0].h, 0) && near(p.pages[3].h, 0));
        check("paged: a page that fits is centred both ways",
              near(p.pages[2].x, std::floor((1000 - 300) / 2.0)) && near(p.pages[2].y, std::floor((700 - 400) / 2.0)));
        check("paged: the content is exactly the viewport when it fits", near(p.width, 1000) && near(p.height, 700));
        auto v = L::visible(p, 0, 700);
        check("paged: visible() sees just that page", v.first == 2 && v.second == 2);
        auto z = L::place_row(pg, 2, 2.0, 1000, 700);
        check("paged: zoomed past the view, it starts at the margin and scrolls",
              near(z.pages[2].y, L::kMargin) && near(z.height, 1600 + 2 * L::kMargin));
        auto b = L::place_row(pg, 4, 0.5, 1000, 700, M::Book);
        check("paged book: the facing pair 3|4 (pages 4|5) together",
              near(b.pages[3].h, 400) && near(b.pages[4].h, 400) && near(b.pages[3].y, b.pages[4].y) &&
              near(b.pages[2].h, 0) && near(b.pages[5].h, 0));
        auto c = L::place_row(pg, 0, 0.5, 1000, 700, M::Book);
        check("paged book: the cover alone, right of the spine",
              near(c.pages[0].x, std::floor(c.width / 2) + L::kSpine / 2) && near(c.pages[1].h, 0));
        check("paged: fit page of a facing pair fits the pair",
              near(L::fit_page(pg, 3, 1250, 900, M::Book), std::min((1250 - 2 * L::kMargin - L::kSpine) / 1200.0,
                                                                   (900 - 2 * L::kMargin) / 800.0)));
        check("paged: an empty doc places nothing", L::place_row({}, 0, 1, 800, 600).pages.empty());
    }

    // ── Lookit: find-in-document ────────────────────────────────────────────
    {
        namespace S = core::search;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        auto r = S::from_pdf(100, 700, 160, 712, 792);
        check("search: poppler's bottom-left rect -> top-left", near(r.x, 100) && near(r.y, 80) &&
                                                                 near(r.w, 60) && near(r.h, 12));
        auto r2 = S::from_pdf(160, 712, 100, 700, 792);
        check("search: corners in either order", near(r2.x, 100) && near(r2.y, 80));
        std::vector<S::Hit> h{{2, {50, 100, 10, 12}}, {0, {300, 40, 10, 12}}, {0, {20, 41, 10, 12}},
                              {0, {20, 400, 10, 12}}};
        S::sort(h);
        check("search: page order, then lines, then left to right",
              h[0].page == 0 && near(h[0].r.x, 20) && near(h[1].r.x, 300) && near(h[2].r.y, 400) &&
              h[3].page == 2);
        check("search: step wraps both ways", S::step(h, 3, +1) == 0 && S::step(h, 0, -1) == 3 &&
                                              S::step(h, 1, +1) == 2);
        check("search: step from no current starts at an end", S::step(h, -1, +1) == 0 && S::step(h, -1, -1) == 3);
        check("search: no hits, nowhere to go", S::step({}, 0, +1) == -1 && S::first_from({}, 3) == -1);
        check("search: first hit from the page you're on", S::first_from(h, 1) == 3 && S::first_from(h, 0) == 0);
        check("search: past the last hit wraps to the first", S::first_from(h, 5) == 0);
    }


    // ── Lookit: page tools -- the edited page list, through the one door ────
    {
        using core::Pages;
        namespace P = core::pages;
        Pages pg;
        core::PageJournal j{pg};
        pg.set_door([&] { return j.open(); });
        pg.load("/a.pdf", 5);
        auto order = [&] {
            std::string o;
            for (const auto& r : pg.records()) o += std::to_string(r.src) + ":" + std::to_string(r.index + 1) +
                                                     (r.turn ? "^" + std::to_string(r.turn) : "") + " ";
            return o;
        };
        check("pages: a load is pages 1..5, no undo step", order() == "0:1 0:2 0:3 0:4 0:5 " && !j.can_undo());
        check("pages: rotate right", P::rotate(j, pg, {pg.id_at(1)}, 1) && order() == "0:1 0:2^1 0:3 0:4 0:5 ");
        check("pages: rotate left wraps to three quarters",
              P::rotate(j, pg, {pg.id_at(0)}, -1) && order() == "0:1^3 0:2^1 0:3 0:4 0:5 ");
        check("pages: rotate by 0 is not a step", !P::rotate(j, pg, {pg.id_at(0)}, 4));
        check("pages: undo the rotation", j.undo().done && order() == "0:1 0:2^1 0:3 0:4 0:5 ");
        check("pages: redo it", j.redo().done && order() == "0:1^3 0:2^1 0:3 0:4 0:5 ");
        check("pages: move page 5 to the front", P::move(j, pg, {pg.id_at(4)}, 0) && order() == "0:5 0:1^3 0:2^1 0:3 0:4 ");
        check("pages: move two pages to the end",
              P::move(j, pg, {pg.id_at(0), pg.id_at(1)}, 5) && order() == "0:2^1 0:3 0:4 0:5 0:1^3 ");
        check("pages: a move to where it is is not a step", !P::move(j, pg, {pg.id_at(0)}, 0));
        check("pages: delete page 2", P::remove(j, pg, {pg.id_at(1)}) && pg.count() == 4 &&
                                       order() == "0:2^1 0:4 0:5 0:1^3 ");
        check("pages: never delete every page", !P::remove(j, pg, pg.ids()) && pg.count() == 4);
        check("pages: undo the delete puts it back in its place",
              j.undo().done && order() == "0:2^1 0:3 0:4 0:5 0:1^3 ");
        const int b = pg.add_source("/b.pdf");
        auto made = P::insert(j, pg, b, 2, 1, "b.pdf");
        check("pages: insert 2 pages of b.pdf after page 1",
              made.size() == 2 && order() == "0:2^1 1:1 1:2 0:3 0:4 0:5 0:1^3 ");
        check("pages: undo the insert", j.undo().done && pg.count() == 5);
        check("pages: redo the insert", j.redo().done && pg.count() == 7 && order() == "0:2^1 1:1 1:2 0:3 0:4 0:5 0:1^3 ");
        check("pages: duplicate a page lands right after it",
              P::duplicate(j, pg, pg.id_at(0)) && order() == "0:2^1 0:2^1 1:1 1:2 0:3 0:4 0:5 0:1^3 ");
        check("pages: undo says what it undoes", j.undo_label() == "Duplicate page");

        // ── s008 / s009: marks ride in their page's record ──
        {
            const auto id = pg.id_at(2);
            core::Mark s;
            s.art = "/sig.png"; s.x = 100; s.y = 600; s.w = 200; s.h = 50;
            check("sign: add a signature is one step",
                  P::add_mark(j, pg, id, s) && pg.get(id)->marks.size() == 1 && j.undo_label() == "Add signature");
            check("sign: no picture, no signature", !P::add_mark(j, pg, id, core::Mark{}));
            auto moved = s;
            moved.x = 140;
            check("sign: move it", P::set_mark(j, pg, id, 0, moved, "Move") &&
                                   pg.get(id)->marks[0].x == 140 && j.undo_label() == "Move signature");
            check("sign: setting it to itself is no step", !P::set_mark(j, pg, id, 0, moved, "Move"));
            check("sign: undo the move", j.undo().done && pg.get(id)->marks[0].x == 100);
            check("sign: redo the move", j.redo().done && pg.get(id)->marks[0].x == 140);
            check("sign: a page turn keeps its signature",
                  P::rotate(j, pg, {id}, 1) && pg.get(id)->marks.size() == 1 && pg.get(id)->turn == 1);
            j.undo();
            const int before = pg.count();
            check("sign: a duplicated page is a signed page",
                  P::duplicate(j, pg, id) && pg.count() == before + 1 && pg.get(pg.id_at(3))->marks.size() == 1);
            j.undo();
            check("sign: deleting the page and undoing brings the signature back",
                  P::remove(j, pg, {id}) && !pg.get(id) && j.undo().done && pg.get(id)->marks[0].x == 140);
            check("sign: delete the signature", P::remove_mark(j, pg, id, 0) && pg.get(id)->marks.empty() &&
                                                j.undo_label() == "Delete signature");
            check("sign: undo puts it back, exactly", j.undo().done && pg.get(id)->marks.size() == 1 &&
                                                      pg.get(id)->marks[0] == moved);
            check("sign: a mark index out of range is refused",
                  !P::remove_mark(j, pg, id, 3) && !P::set_mark(j, pg, id, -1, s, ""));

            // s009: markup through the same door, labelled by kind.
            core::Mark arrow;
            arrow.kind = core::MarkKind::Arrow; arrow.x = 10; arrow.y = 10; arrow.w = -5; arrow.h = 40;
            check("markup: an arrow (ends may run backwards) is a step",
                  P::add_mark(j, pg, id, arrow) && j.undo_label() == "Add arrow");
            core::Mark dot = arrow;
            dot.w = dot.h = 0;
            check("markup: a line of no length is refused", !P::add_mark(j, pg, id, dot));
            auto red = arrow;
            red.color = 0xe01b24ffu;
            check("markup: recolour is one step, named",
                  P::set_mark(j, pg, id, 1, red, "Recolour") && j.undo_label() == "Recolour arrow");
            core::Mark note;
            note.kind = core::MarkKind::Note; note.x = 5; note.y = 5; note.w = note.h = 20; note.text = "hi";
            P::add_mark(j, pg, id, note);
            auto edited = note;
            edited.text = "hello";
            check("markup: edit a note's text", P::set_mark(j, pg, id, 2, edited, "Edit") &&
                                                 pg.get(id)->marks[2].text == "hello" && j.undo_label() == "Edit note");
            check("markup: undo the edit", j.undo().done && pg.get(id)->marks[2].text == "hi");
            check("markup: delete the arrow keeps the order of the rest",
                  P::remove_mark(j, pg, id, 1) && pg.get(id)->marks.size() == 2 &&
                  pg.get(id)->marks[1].kind == core::MarkKind::Note && j.undo_label() == "Delete arrow");
            check("markup: undo puts it back in its place",
                  j.undo().done && pg.get(id)->marks[1].kind == core::MarkKind::Arrow);
        }
        check("pages: THE GATE -- no write went around the journal", pg.strays() == 0);
        pg.put(pg.id_at(0), std::nullopt);
        check("pages: the gate catches a write outside the door", pg.strays() == 1);

        std::string err;
        auto r = P::parse_ranges("3-5, 1, 9-", 10, err);
        check("ranges: 3-5, 1, 9-", err.empty() && r == std::vector<int>{2, 3, 4, 0, 8, 9});
        check("ranges: repeats once, reverse ranges allowed",
              P::parse_ranges("2,2,4-3", 5, err) == std::vector<int>{1, 3, 2});
        check("ranges: -3 means 1-3", P::parse_ranges("-3", 5, err) == std::vector<int>{0, 1, 2});
        check("ranges: out of range is refused with a reason", P::parse_ranges("7", 5, err).empty() && !err.empty());
        check("ranges: junk is refused", P::parse_ranges("x", 5, err).empty() && !err.empty());
        check("ranges: empty is empty, no error", P::parse_ranges(" ", 5, err).empty() && err.empty());
    }
    {
        namespace S = core::search;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        const core::layout::Rect r{10, 20, 30, 5};   // on a 100 x 200 page
        auto t1 = S::turn_rect(r, 100, 200, 1);
        check("search: a hit on a page turned right", near(t1.x, 175) && near(t1.y, 10) && near(t1.w, 5) && near(t1.h, 30));
        auto t2 = S::turn_rect(r, 100, 200, 2);
        check("search: ... turned over", near(t2.x, 60) && near(t2.y, 175));
        auto t3 = S::turn_rect(r, 100, 200, 3);
        check("search: ... turned left", near(t3.x, 20) && near(t3.y, 60) && near(t3.w, 5));
        auto t4 = S::turn_rect(r, 100, 200, 4);
        check("search: four turns is none", near(t4.x, 10) && near(t4.y, 20));
    }

    // ── Lookit s008 / s009: where a mark sits, and the export arithmetic ─────
    {
        namespace G = core::sign;
        namespace M = core::marks;
        auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
        auto same = [&](const core::layout::Rect& a, const core::layout::Rect& b) {
            return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
        };
        core::Mark s;
        s.kind = core::MarkKind::Box; s.x = 10; s.y = 20; s.w = 30; s.h = 5;
        bool round = true, pts = true, vecs = true;
        for (int t = 0; t < 4; ++t) {
            const auto v = M::view_box(s, 100, 200, t);
            round = round && M::with_view_box(s, v, 100, 200, t) == s;
            const M::Point p{13, 71};
            const auto q = M::unturn_point(M::turn_point(p, 100, 200, t), 100, 200, t);
            pts = pts && near(q.x, p.x) && near(q.y, p.y);
            // a drag on screen moves the mark the same way on screen
            const auto mv = M::view_box(M::moved(s, 7, -3, t), 100, 200, t);
            vecs = vecs && near(mv.x, v.x + 7) && near(mv.y, v.y - 3);
        }
        check("marks: view_box / with_view_box are inverses at every turn", round);
        check("marks: turn_point / unturn_point are inverses at every turn", pts);
        check("marks: a drag moves a mark with the pointer at every turn", vecs);
        check("marks: a box on a page turned right", same(M::view_box(s, 100, 200, 1), {175, 10, 5, 30}));
        core::Mark ln;
        ln.kind = core::MarkKind::Arrow; ln.x = 10; ln.y = 10; ln.w = 20; ln.h = -5;
        const auto [la, lb] = M::view_line(ln, 100, 200, 1);
        check("marks: a line's ends turn with the page, the head stays the head",
              near(la.x, 190) && near(la.y, 10) && near(lb.x, 195) && near(lb.y, 30));
        const auto back = M::with_view_line(ln, la, lb, 100, 200, 1);
        check("marks: ... and come back", near(back.x, 10) && near(back.w, 20) && near(back.h, -5));
        auto p0 = M::signature("/a.png", 300, 100, 612, 792, 0);
        check("sign: placed a third of the width, centred, two thirds down, upright",
              near(p0.w, 204) && near(p0.h, 68) && near(p0.x, 204) && near(p0.y, 528 - 34) && p0.turn == 0);
        auto p1 = M::signature("/a.png", 300, 100, 612, 792, 1);
        const auto v1 = M::view_box(p1, 612, 792, 1);
        check("sign: on a page turned right it reads upright to you",
              M::view_turn(p1, 1) == 0 && near(v1.w, 264) && near(v1.h, 88) && near(v1.x, (792 - 264) / 2.0));
        auto tall = M::signature("/a.png", 100, 300, 612, 792, 0);
        check("sign: a tall picture is held to a sixth of the height", near(tall.h, 132) && near(tall.w, 44));
        auto tx = M::boxed(core::MarkKind::Text, {50, 60, 100, 20}, 0xff, 1, 612, 792, 3);
        check("markup: text on a page turned left reads upright", M::view_turn(tx, 3) == 0 &&
                                                               same(M::view_box(tx, 612, 792, 3), {50, 60, 100, 20}));
        auto hl = M::highlight({{10, 10, 100, 12}, {10, 24, 60, 12}}, {0, 0, 1, 1}, 0xffd400ffu, 612, 792, 0);
        check("markup: a highlight is its lines, boxed by their union",
              hl.parts.size() == 2 && same({hl.x, hl.y, hl.w, hl.h}, {10, 10, 100, 26}));
        auto hl2 = M::highlight({}, {5, 5, 50, 10}, 0xffd400ffu, 612, 792, 0);
        check("markup: no text under it -> the swept box", hl2.parts.size() == 1 && same(hl2.parts[0], {5, 5, 50, 10}));
        auto hm = M::moved(hl, 5, 5, 0);
        check("markup: moving a highlight moves its lines", near(hm.parts[1].x, 15) && near(hm.parts[1].y, 29));
        auto nt = M::note({600, 790}, "x", 0xffd400ffu, 612, 792, 0);
        check("markup: a note clicked at the edge stays on the page",
              near(nt.x, 612 - M::kNoteSize) && near(nt.y, 792 - M::kNoteSize));
        check("markup: kinds -- what can be sized",
              !M::resizable(core::MarkKind::Highlight) && !M::resizable(core::MarkKind::Note) &&
              M::resizable(core::MarkKind::Box) && M::is_line(core::MarkKind::Arrow));
        check("markup: every kind has a name and a colour",
              std::string(M::noun(core::MarkKind::Ellipse)) == "ellipse" &&
              M::default_color(core::MarkKind::Highlight) == M::palette()[0]);
        const core::layout::Rect b{100, 100, 200, 50};
        check("sign: move", same(G::move(b, 10, -20, 612, 792), {110, 80, 200, 50}));
        check("sign: move stays on the page", same(G::move(b, 1000, -1000, 612, 792), {412, 0, 200, 50}));
        const auto r = G::resize(b, 100, 0, 612, 792);
        check("sign: resize keeps the aspect", near(r.w, 300) && near(r.h, 75) && near(r.x, 100));
        check("sign: resize stops at the page edge", near(G::resize(b, 1000, 0, 612, 792).w, 512));
        check("sign: resize never below the minimum", near(G::resize(b, -1000, -1000, 612, 792).h, 12));
        check("markup: free resize -- each side its own way",
              same(G::resize_free(b, 50, -20, 612, 792), {100, 100, 250, 30}) &&
              near(G::resize_free(b, -1000, 0, 612, 792).w, 6));
        std::vector<M::Shape> shapes(2);
        shapes[0].box = {0, 0, 100, 40};
        shapes[1].box = {50, 20, 100, 40};
        check("marks: hit the top one first", M::hit(shapes, 60, 30, 9).index == 1);
        check("marks: hit the corner", M::hit(shapes, 148, 58, 9).part == M::Part::Corner);
        shapes[1].resizable = false;
        check("marks: no corner when it can't be sized", M::hit(shapes, 148, 58, 9).part == M::Part::Body);
        check("marks: a miss", M::hit(shapes, 300, 300, 9).index == -1);
        std::vector<M::Shape> lines(1);
        lines[0].line = true; lines[0].a = {0, 0}; lines[0].b = {100, 100}; lines[0].box = {0, 0, 100, 100};
        check("marks: a line is hit near it, not in its box",
              M::hit(lines, 52, 48, 5).part == M::Part::Body && M::hit(lines, 80, 20, 5).index == -1);
        check("marks: a line's ends are handles",
              M::hit(lines, 1, 1, 5).part == M::Part::Start && M::hit(lines, 99, 98, 5).part == M::Part::End);
        const auto pd = M::to_pdf({10, 20}, 0, 0, 612, 792, 0);
        check("save: a note's corner in PDF space (y up)", near(pd.x, 10) && near(pd.y, 772));
        const auto pc = M::to_pdf({10, 20}, 50, 100, 500, 700, 0);
        check("save: ... on a cropped page", near(pc.x, 60) && near(pc.y, 680));
        const auto pr = M::to_pdf({10, 20}, 0, 0, 612, 792, 90);   // seen 792 x 612
        check("save: ... on a page the file turns right", near(pr.x, 20) && near(pr.y, 10));

        // A scan: white paper, a grey edge, black ink.
        std::vector<std::uint8_t> px{255, 255, 255, 255,  168, 168, 168, 255,  20, 20, 40, 255,  0, 0, 0, 0};
        G::ink(px.data(), 4, 1, 16);
        check("sign: ink -- paper goes clear", px[3] == 0);
        check("sign: ink -- an edge is half there", px[7] > 60 && px[7] < 200);
        check("sign: ink -- ink stays solid, its colour kept", px[11] == 255 && px[8] == 20 && px[10] == 40);
        check("sign: ink -- clear stays clear", px[15] == 0);
        const auto ib = G::ink_bounds(px.data(), 4, 1, 16);
        check("sign: ink bounds", near(ib.x, 1) && near(ib.w, 2));
        std::vector<std::uint8_t> blank(16, 0);
        check("sign: no ink, no bounds", G::ink_bounds(blank.data(), 4, 1, 16).w == 0);
    }
    {
        namespace C = core::convert;
        // One ARGB32 pixel per case: opaque red, half-clear (premultiplied) green, clear.
        const std::uint32_t src[3] = {0xffff0000u, 0x80008000u, 0x00000000u};
        std::uint8_t out[12];
        C::argb32_to_rgba(reinterpret_cast<const std::uint8_t*>(src), 12, 3, 1, out, 12, false);
        check("convert: opaque stays", out[0] == 255 && out[1] == 0 && out[3] == 255);
        check("convert: half-clear is un-premultiplied", out[5] == 255 && out[7] == 128);
        check("convert: clear is clear", out[11] == 0);
        C::argb32_to_rgba(reinterpret_cast<const std::uint8_t*>(src), 12, 3, 1, out, 12, true);
        check("convert: flattened onto white", out[4] == 127 && out[5] == 255 && out[6] == 127 && out[7] == 255 &&
                                               out[8] == 255 && out[11] == 255);
        check("convert: page names pad to the count",
              C::page_name("Report", 2, 12, "png") == "Report-p03.png" &&
              C::page_name("Report", 99, 120, "jpg") == "Report-p100.jpg");
        check("convert: a single page has no number", C::page_name("Card", 0, 1, "png") == "Card.png");
        std::set<std::string> taken{"/x/a.png", "/x/a (2).png"};
        auto exists = [&](const std::string& p) { return taken.count(p) > 0; };
        check("convert: a free name is kept", C::unique_name("/x/b.png", exists) == "/x/b.png");
        check("convert: a taken name gets a number", C::unique_name("/x/a.png", exists) == "/x/a (3).png");
        check("convert: a dot in a folder is not an extension",
              C::unique_name("/x.y/a", [](const std::string& p) { return p == "/x.y/a"; }) == "/x.y/a (2)");
        const auto px = C::pixels(612, 792, 150);
        check("convert: Letter at 150 dpi is 1275 x 1650", px.first == 1275 && px.second == 1650);
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

    // ── s010: picture tools (core/Picture) ──────────────────────────────────
    std::cout << "picture\n";
    {
        namespace PI = core::picture;
        using core::PicEdit;
        const int W = 40, H = 20;   // a wide picture
        auto near = [](PI::Pt a, PI::Pt b) { return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9; };
        auto rnear = [](const core::layout::Rect& a, const core::layout::Rect& b) {
            return std::abs(a.x - b.x) < 1e-9 && std::abs(a.y - b.y) < 1e-9 && std::abs(a.w - b.w) < 1e-9 &&
                   std::abs(a.h - b.h) < 1e-9;
        };
        PicEdit e;
        check("picture: untouched is the picture", PI::out_size(e, W, H) == std::pair{W, H} && PI::unchanged(e));
        const auto r1 = PI::turned(e, 1, W, H);
        check("picture: a right turn swaps the sides", PI::out_size(r1, W, H) == std::pair{H, W});
        check("picture: right turn -- the top-left corner goes top-right",
              near(PI::orient_point({0, 0}, r1, W, H), {double(H), 0}));
        check("picture: four turns are none", PI::turned(PI::turned(r1, 1, W, H), 2, W, H) == e);
        check("picture: left then right is none", PI::turned(PI::turned(e, -1, W, H), 1, W, H) == e);

        // Every flip / turn, from every orientation, must equal mirroring /
        // turning what you see -- the group arithmetic in flipped().
        bool group_ok = true;
        const PI::Pt probes[] = {{0, 0}, {3, 1}, {double(W), 0}, {7, double(H)}, {double(W), double(H)}};
        for (int t = 0; t < 4; ++t)
            for (int f = 0; f < 2; ++f) {
                PicEdit o; o.turn = t; o.flip = f;
                const auto [ow, oh] = PI::oriented(o, W, H);
                for (bool horiz : {true, false}) {
                    const auto fl = PI::flipped(o, horiz, W, H);
                    for (auto p : probes) {
                        const auto a = PI::orient_point(p, o, W, H);
                        const PI::Pt want = horiz ? PI::Pt{ow - a.x, a.y} : PI::Pt{a.x, oh - a.y};
                        if (!near(PI::orient_point(p, fl, W, H), want)) group_ok = false;
                    }
                }
                const auto tr = PI::turned(o, 1, W, H);
                for (auto p : probes) {
                    const auto a = PI::orient_point(p, o, W, H);
                    if (!near(PI::orient_point(p, tr, W, H), PI::Pt{oh - a.y, a.x})) group_ok = false;
                }
            }
        check("picture: flip / turn from all 8 orientations mirror / turn what you see", group_ok);
        check("picture: flipping twice is none",
              PI::flipped(PI::flipped(r1, true, W, H), true, W, H) == r1 &&
              PI::flipped(PI::flipped(r1, false, W, H), false, W, H) == r1);

        const auto c1 = PI::cropped(e, {10, 5, 20, 10}, W, H);
        check("picture: crop keeps what you framed",
              rnear(c1.crop, {10, 5, 20, 10}) && PI::out_size(c1, W, H) == std::pair{20, 10});
        check("picture: crop the whole picture is no crop", PI::cropped(e, {0, 0, W, H}, W, H) == e);
        check("picture: a crop under a pixel is refused", PI::cropped(e, {3, 3, 0.4, 5}, W, H) == e);
        const auto c2 = PI::cropped(c1, {5, 0, 10, 5}, W, H);
        check("picture: a crop of a crop lands in the picture", rnear(c2.crop, {15, 5, 10, 5}));
        const auto c1r = PI::turned(c1, 1, W, H);
        check("picture: a crop turns with the picture",
              rnear(c1r.crop, {5, 10, 10, 20}) && PI::out_size(c1r, W, H) == std::pair{10, 20});
        check("picture: a crop mirrors with the picture",
              rnear(PI::flipped(c1, true, W, H).crop, {10, 5, 20, 10}) &&
              rnear(PI::flipped(PI::cropped(e, {0, 0, 10, 20}, W, H), true, W, H).crop, {30, 0, 10, 20}));

        const auto z = PI::resized(e, 20, 10, W, H);
        check("picture: resize", PI::out_size(z, W, H) == std::pair{20, 10});
        check("picture: resize to its own size is none", PI::resized(z, W, H, W, H) == e);
        const auto zc = PI::cropped(z, {0, 0, 10, 10}, W, H);   // half the width of the half-size picture
        check("picture: crop after resize keeps the scale",
              rnear(zc.crop, {0, 0, 20, 20}) && PI::out_size(zc, W, H) == std::pair{10, 10});
        check("picture: resize turns with the picture", PI::out_size(PI::turned(z, 1, W, H), W, H) == std::pair{10, 20});
        check("picture: keep_aspect", PI::keep_aspect(20, 40, 30) == 15 && PI::keep_aspect(1, 4000, 10) == 1);

        // The light pass on ARGB32 pixels.
        std::uint32_t px[4] = {0xff808080u, 0xffff0000u, 0x00000000u, 0x80404040u};
        PI::light_argb32(reinterpret_cast<std::uint8_t*>(px), 4, 1, 16, 0, 0, 0);
        check("picture: no light changes nothing", px[0] == 0xff808080u && px[3] == 0x80404040u);
        PI::light_argb32(reinterpret_cast<std::uint8_t*>(px), 4, 1, 16, 50, 0, 0);
        check("picture: brightness lifts grey", (px[0] & 0xff) > 0xb0 && (px[0] >> 24) == 0xff);
        check("picture: a clear pixel stays clear", px[2] == 0);
        check("picture: a part-clear pixel keeps its alpha and stays premultiplied",
              (px[3] >> 24) == 0x80 && (px[3] & 0xff) <= 0x80 && (px[3] & 0xff) > 0x40);
        std::uint32_t red[1] = {0xffc03030u};
        PI::light_argb32(reinterpret_cast<std::uint8_t*>(red), 1, 1, 4, 0, 0, -100);
        check("picture: saturation -100 is grey",
              ((red[0] >> 16) & 255) == ((red[0] >> 8) & 255) && ((red[0] >> 8) & 255) == (red[0] & 255));
        std::uint32_t mid[2] = {0xff606060u, 0xffa0a0a0u};
        PI::light_argb32(reinterpret_cast<std::uint8_t*>(mid), 2, 1, 8, 0, 50, 0);
        check("picture: contrast pushes away from mid-grey", (mid[0] & 255) < 0x60 && (mid[1] & 255) > 0xa0);

        // The crop frame on screen.
        const core::layout::Rect f{10, 10, 100, 50};
        check("picture: crop grips",
              PI::crop_hit(f, 110, 60, 6) == PI::Grip::BottomRight && PI::crop_hit(f, 11, 9, 6) == PI::Grip::TopLeft &&
              PI::crop_hit(f, 50, 30, 6) == PI::Grip::Body && PI::crop_hit(f, 300, 300, 6) == PI::Grip::None);
        check("picture: dragging the frame keeps it on the picture",
              rnear(PI::crop_drag(f, PI::Grip::Body, 500, -500, 200, 100), {100, 0, 100, 50}));
        check("picture: a corner sizes, the opposite corner stays",
              rnear(PI::crop_drag(f, PI::Grip::TopLeft, 20, 5, 200, 100), {30, 15, 80, 45}) &&
              rnear(PI::crop_drag(f, PI::Grip::BottomRight, -1000, 0, 200, 100), {10, 10, 8, 50}));

        // Browsing the folder.
        const auto names = PI::sorted({"img10.jpg", "IMG2.png", "img1.jpg", "b.png", "a.png"});
        check("picture: natural order (img2 before img10, case-blind)",
              names == std::vector<std::string>{"a.png", "b.png", "img1.jpg", "IMG2.png", "img10.jpg"});
        check("picture: next / previous, no wrap",
              PI::step(names, "IMG2.png", +1) == "img10.jpg" && PI::step(names, "IMG2.png", -1) == "img1.jpg" &&
              PI::step(names, "img10.jpg", +1).empty() && PI::step(names, "a.png", -1).empty() &&
              PI::step(names, "gone.png", +1).empty());
        check("picture: 007 sorts as 7", PI::natural_less("p7.png", "p007b.png") && PI::natural_less("p007.png", "p8.png"));

        // The journal: every verb one step, undone exactly, nothing by a side door.
        core::PicStore st;
        core::PicJournal j{st};
        st.set_door([&] { return j.open(); });
        st.load(W, H);
        namespace PV = core::pic;
        const bool ran = PV::rotate(j, st, 1) && PV::flip(j, st, true) && PV::crop(j, st, {2, 2, 10, 10}) &&
                         PV::resize(j, st, 5, 5);
        check("picture verbs: rotate, flip, crop, resize are steps", ran && j.undo_label() == "Resize");
        const auto t0 = std::chrono::steady_clock::now();
        j.run("Adjust Light", {0}, [&] { st.put(0, PI::with_light(st.edit(), 10, 0, 0)); }, "light-1", t0);
        j.run("Adjust Light", {0}, [&] { st.put(0, PI::with_light(st.edit(), 20, 0, 0)); }, "light-1",
              t0 + std::chrono::milliseconds(200));
        check("picture verbs: slider moves merge into one step", st.edit().brightness == 20 && j.undo_label() == "Adjust Light");
        j.undo();
        check("picture verbs: undo the light in one", st.edit().brightness == 0 && st.edit().out_w == 5);
        check("picture verbs: a no-op is not a step", !PV::resize(j, st, 5, 5) && !PV::rotate(j, st, 4));
        check("picture verbs: a size of 0 is refused", !PV::resize(j, st, 0, 10));
        while (j.can_undo()) j.undo();
        check("picture verbs: undo all is the picture as it came", PI::unchanged(st.edit()));
        while (j.can_redo()) j.redo();
        check("picture verbs: redo all comes back", st.edit().out_w == 5 && st.edit().flip);
        const bool reverted = PV::reset(j, st) && PI::unchanged(st.edit());
        check("picture verbs: revert is one step", reverted && j.undo().done && st.edit().out_w == 5);
        check("picture verbs: no write went round the door", st.strays() == 0);
        st.put(0, PicEdit{});
        check("picture verbs: the gate sees a stray write", st.strays() == 1);
    }

    // ── s011: forms and the lock -- the document's own record, same journal ──
    {
        using core::Pages;
        using core::Field;
        using core::FieldKind;
        namespace F = core::forms;
        namespace P = core::pages;
        Pages pg;
        core::PageJournal j{pg};
        pg.set_door([&] { return j.open(); });
        pg.load("/form.pdf", 2);
        check("forms: the document record is not a page",
              pg.count() == 2 && pg.ids().size() == 2 && pg.get(Pages::kDoc).has_value() && pg.id_at(0) != Pages::kDoc);
        check("forms: a load starts with no values and the file's own lock",
              pg.doc().fields.empty() && pg.doc().lock.mode == core::Lock::Mode::Keep);

        Field name; name.src = 0; name.id = 1; name.name = "name"; name.kind = FieldKind::Text;
        name.box = {150, 97, 250, 20}; name.value = "Ann";
        Field agree; agree.src = 0; agree.id = 2; agree.name = "agree"; agree.kind = FieldKind::Check;
        agree.box = {150, 139, 16, 16}; agree.value = "0";
        Field small; small.src = 0; small.id = 3; small.name = "size"; small.kind = FieldKind::Radio;
        small.box = {250, 179, 16, 16}; small.value = "0";
        Field large = small; large.id = 4; large.box.x = 300; large.value = "1";
        Field colour; colour.src = 0; colour.id = 5; colour.name = "colour"; colour.kind = FieldKind::Combo;
        colour.options = {"Red", "Green", "Blue"}; colour.value = "#1"; colour.box = {150, 217, 150, 20};
        Field locked = name; locked.id = 6; locked.name = "id"; locked.read_only = true;
        const std::vector<Field> all{name, agree, small, large, colour, locked};
        auto val = [&](const Field& f) {
            const auto it = pg.doc().fields.find(F::key(f.src, f.id));
            return it == pg.doc().fields.end() ? f.value : it->second;
        };

        check("forms: fill in a text field is one step",
              F::set(j, pg, name, all, "Bob") && val(name) == "Bob" && j.undo_label() == "Fill in “name”");
        check("forms: the same words again is no step", !F::set(j, pg, name, all, "Bob"));
        check("forms: tick a box", F::set(j, pg, agree, all, F::on(true)) && F::is_on(val(agree)) &&
                                   j.undo_label() == "Tick “agree”");
        check("forms: a radio turns the rest of its group off",
              F::set(j, pg, small, all, F::on(true)) && val(small) == "1" && val(large) == "0");
        check("forms: choosing the radio that's on is no step", !F::set(j, pg, small, all, F::on(true)));
        check("forms: a drop-down takes an item by number",
              F::set(j, pg, colour, all, F::picks({2})) && F::shown(colour, val(colour)) == "Blue");
        check("forms: a read-only field is refused", !F::set(j, pg, locked, all, "x") && !F::fillable(locked));
        check("forms: four steps, undone in order",
              j.undo().done && val(colour) == "#1" && j.undo().done && val(small) == "0" && val(large) == "1" &&
              j.undo().done && val(agree) == "0" && j.undo().done && val(name) == "Ann");
        check("forms: redo all brings them back", j.redo().done && j.redo().done && j.redo().done && j.redo().done &&
                                                  val(name) == "Bob" && val(small) == "1" && F::changed(pg) == 5);
        check("forms: page edits and fills share one journal",
              P::rotate(j, pg, {pg.id_at(1)}, 1) && j.undo().done && j.undo_label() == "Choose “colour”");
        check("forms: clear puts the file's value back in one step",
              F::reset(j, pg, name, all) && val(name) == "Ann" && j.undo_label() == "Clear “name”" &&
              !F::reset(j, pg, name, all));
        check("forms: a page delete leaves the values alone",
              P::remove(j, pg, {pg.id_at(0)}) && val(small) == "1" && j.undo().done);

        // Values: encodings and what a person reads.
        check("forms: picks sort and drop repeats", F::picks({2, 0, 2}) == "#0,2" && F::picked("#0,2") == std::vector<int>{0, 2});
        check("forms: picked ignores what isn't picks", F::picked("Blue").empty() && F::picked("#").empty() &&
                                                        F::picked("#1,x,3") == std::vector<int>{1, 3});
        check("forms: typed words in a combo", F::is_typed(F::typed("Teal")) && F::typed_words("=Teal") == "Teal" &&
                                               F::shown(colour, "=Teal") == "Teal");
        check("forms: shown for a list and a box", F::shown(colour, "#0,1") == "Red, Green" && F::shown(agree, "1") == "on");

        // Hit and Tab order.
        Field inner = colour; inner.id = 9; inner.box = {160, 220, 20, 10};
        check("forms: hit finds the field under the point", F::hit(all, 151, 100) == 0 && F::hit(all, 10, 10) < 0);
        check("forms: a field inside another wins", F::hit({colour, inner}, 165, 225) == 1);
        std::vector<std::pair<int, Field>> tab{{1, name}, {0, large}, {0, small}, {0, name}};
        tab[2].second.box.y = 180.5;   // small sits on large's line, a hair lower
        check("forms: Tab goes by page, line, then left to right", F::tab_order(tab) == std::vector<int>{3, 2, 1, 0});

        // The lock.
        using L = core::Lock;
        check("lock: add a password is one step",
              P::set_lock(j, pg, {L::Mode::Password, "s3cret"}) && pg.doc().lock.password == "s3cret" &&
              j.undo_label() == "Add password");
        check("lock: an empty password is refused", !P::set_lock(j, pg, {L::Mode::Password, ""}));
        check("lock: change it", P::set_lock(j, pg, {L::Mode::Password, "other"}) && j.undo_label() == "Change password");
        check("lock: remove it", P::set_lock(j, pg, {L::Mode::None, ""}) && j.undo_label() == "Remove password");
        check("lock: the same lock again is no step", !P::set_lock(j, pg, {L::Mode::None, ""}));
        check("lock: undo walks back to the file's own",
              j.undo().done && j.undo().done && j.undo().done && pg.doc().lock.mode == L::Mode::Keep);
        check("lock: the form values survive the lock's steps", val(name) == "Ann" && val(small) == "1");
        check("forms: no write went round the door", pg.strays() == 0);
        auto d = pg.doc();
        d.fields["0:1"] = "sneak";
        pg.put(Pages::kDoc, d);
        check("forms: the gate sees a stray write to the document record", pg.strays() == 1);
    }

    // ── s012: books and comics -- ZIP, comic pages, EPUB, DjVu words ─────────
    {
        namespace Z = core::zip;
        namespace C = core::comic;
        namespace E = core::epub;
        namespace W = core::words;
        const std::string png = std::string("\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\x02\x58\0\0\x03\x84", 24);   // 600 x 900
        const std::string arc = Z::make_stored({{"Comic/", ""},
                                                {"Comic/page10.png", png},
                                                {"Comic/page2.png", png},
                                                {"__MACOSX/Comic/._page2.png", "junk"},
                                                {"Comic/.thumb.jpg", "junk"},
                                                {"Comic/notes.txt", "hello"},
                                                {"Comic/page1.PNG", png}});
        std::string err;
        const auto es = Z::list(arc, err);
        check("zip: the directory lists every entry", es.size() == 7 && err.empty() && es[0].dir());
        const auto body = Z::raw(arc, es[size_t(Z::find(es, "Comic/notes.txt"))]);
        check("zip: an entry's bytes come back", body && *body == "hello" && es[5].method == 0 && es[5].size == 5);
        check("zip: find by name, or -1", Z::find(es, "Comic/page2.png") == 2 && Z::find(es, "nope") == -1);
        std::string e2;
        check("zip: not an archive is refused", Z::list("just some text, not a zip file at all", e2).empty() && !e2.empty());
        std::string e3;
        check("zip: a cut-short archive is refused", Z::list(arc.substr(0, arc.size() - 30), e3).empty() && !e3.empty());
        const auto order = C::pages(es);
        std::string names;
        for (int i : order) names += es[size_t(i)].name + " ";
        check("comic: pictures only, natural order, leftovers skipped",
              names == "Comic/page1.PNG Comic/page2.png Comic/page10.png ", names);
        auto sz = C::image_size(png);
        check("comic: PNG size from its header", sz && sz->first == 600 && sz->second == 900);
        const std::string gif = std::string("GIF89a\x40\x01\xf0\x00", 10);
        sz = C::image_size(gif);
        check("comic: GIF size", sz && sz->first == 320 && sz->second == 240);
        // A JPEG: SOI, an APP0 segment, then SOF0 with 1200 x 800 (h first).
        const std::string jpg = std::string("\xff\xd8\xff\xe0\x00\x04\x00\x00\xff\xc0\x00\x0b\x08\x03\x20\x04\xb0\x01\x01\x11\x00", 21);
        sz = C::image_size(jpg);
        check("comic: JPEG size from its frame header", sz && sz->first == 1200 && sz->second == 800);
        std::string bmp(26, '\0');
        bmp[0] = 'B'; bmp[1] = 'M'; bmp[18] = 100; bmp[22] = char(0xce); bmp[23] = char(0xff); bmp[24] = char(0xff); bmp[25] = char(0xff);
        sz = C::image_size(bmp);
        check("comic: BMP size, top-down height", sz && sz->first == 100 && sz->second == 50);
        check("comic: an unknown kind gives no size", !C::image_size("hello there, not a picture at all"));

        check("epub: resolve folds ../ and decodes",
              E::resolve("OEBPS/content.opf", "Text/ch%201.xhtml#top") == "OEBPS/Text/ch 1.xhtml" &&
              E::resolve("OEBPS/Text/a.xhtml", "../Images/c.jpg") == "OEBPS/Images/c.jpg" &&
              E::resolve("content.opf", "/abs/x.html") == "abs/x.html");
        const std::string container =
            "<?xml version=\"1.0\"?><container><rootfiles>"
            "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
            "</rootfiles></container>";
        check("epub: container.xml names the package", E::package_path(container) == "OEBPS/content.opf");
        const std::string opf =
            "<package><metadata><dc:title>The  Lighthouse &amp; Sea</dc:title>"
            "<meta name=\"cover\" content=\"cov\"/></metadata><manifest>"
            "<item id=\"c1\" href=\"Text/one.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"c2\" href=\"Text/two.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\"/>"
            "<item id=\"cov\" href=\"Images/cover.jpg\" media-type=\"image/jpeg\"/></manifest>"
            "<spine><itemref idref=\"c2\"/><itemref idref=\"nav\" linear=\"no\"/><itemref idref=\"c1\"/></spine></package>";
        const auto pk = E::parse_package(opf, "OEBPS/content.opf");
        check("epub: title, spine order (non-linear left out)",
              pk.title == "The Lighthouse & Sea" &&
              pk.spine == std::vector<std::string>{"OEBPS/Text/two.xhtml", "OEBPS/Text/one.xhtml"}, pk.title);
        check("epub: the EPUB 2 cover", pk.cover == "OEBPS/Images/cover.jpg");
        const auto pk3 = E::parse_package(
            "<package><manifest><item id=\"x\" href=\"c.png\" media-type=\"image/png\" properties=\"cover-image\"/>"
            "</manifest><spine/></package>", "content.opf");
        check("epub: the EPUB 3 cover", pk3.cover == "c.png" && pk3.spine.empty());
        const auto bl = E::blocks(
            "<html><head><title>skip me</title><style>p{}</style></head><body>"
            "<h1>Chapter  One</h1><p>It was a <i>dark</i>\n and stormy night&#8212;truly.</p>"
            "<p>Line one<br/>line two &amp; more</p><script>var x;</script><!-- gone --><div>Last</div></body></html>");
        std::vector<E::Block> want{{true, "Chapter One"}, {false, "It was a dark and stormy night—truly."},
                                   {false, "Line one"}, {false, "line two & more"}, {false, "Last"}};
        check("epub: blocks -- headings, paragraphs, entities, head / script / comments skipped", bl == want,
              bl.empty() ? "" : bl[0].text);

        std::vector<W::Word> ws{{"The", {10, 10, 20, 10}}, {"quick", {35, 10, 30, 10}}, {"Brown", {70, 10, 30, 10}},
                                {"fox", {10, 30, 20, 10}}, {"jumps", {35, 30, 30, 10}}};
        check("words: one word, case-blind", W::find(ws, "brown") == std::vector<core::layout::Rect>{{70, 10, 30, 10}});
        check("words: a phrase on one line is one box", W::find(ws, "quick  brown") ==
                                                         std::vector<core::layout::Rect>{{35, 10, 65, 10}});
        check("words: a phrase over a line break is two boxes", W::find(ws, "brown fox").size() == 2);
        check("words: part of a word finds it", W::find(ws, "ump").size() == 1 && W::find(ws, "cat").empty());
    }


    // ── s014: the skin's base colour (core/Tint) ────────────────────────────
    {
        using core::parse_hex; using core::normalise_hex; using core::tint_css;
        const std::string css = lookit::LOOKIT_CSS;
        check("tint: hex -- #rrggbb, rrggbb, #rgb, case", normalise_hex("#15AABF") == "#15aabf" &&
              normalise_hex("15aabf") == "#15aabf" && normalise_hex("#fa0") == "#ffaa00" &&
              normalise_hex(" #000000 ") == "#000000");
        check("tint: not a colour -> nothing", normalise_hex("") == "" && normalise_hex("#12345") == "" &&
              normalise_hex("#gg0000") == "" && normalise_hex("teal") == "");
        check("tint: teal IS the design -- the sheet comes back byte for byte",
              tint_css(css, core::kDefaultTint) == css && tint_css(css, "#15AABF") == css);
        check("tint: a non-colour leaves the sheet alone", tint_css(css, "rainbow") == css);
        check("tint: presets -- teal first and the default, ids unique, all colours",
              core::tint_presets().front().hex == core::kDefaultTint && [] {
                  std::set<std::string> ids;
                  for (const auto& p : core::tint_presets())
                      if (!ids.insert(p.id).second || normalise_hex(p.hex) != p.hex) return false;
                  return true; }());
        check("tint: labels -- a preset's name, else Custom",
              core::tint_label("#e64980") == "Rose" && core::tint_label("#15aabf") == "Teal" &&
              core::tint_label("#123456") == "Custom");

        // WCAG contrast of the skin's text on its backgrounds, before and after.
        auto lum = [](const core::Rgb& c) {
            auto f = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * f(c.r) + 0.7152 * f(c.g) + 0.0722 * f(c.b);
        };
        auto contrast = [&](const core::Rgb& a, const core::Rgb& b) {
            const double x = lum(a), y = lum(b);
            return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
        };
        auto T = [](const char* lit, const std::string& base) {
            return core::tint_color(*parse_hex(lit), *parse_hex(base));
        };
        // Presets, plus the hard customs: pure yellow, pure red, pure blue,
        // black, white, a mid grey, a near-teal.
        std::vector<std::string> bases;
        for (const auto& p : core::tint_presets()) bases.push_back(p.hex);
        for (const char* h : {"#ffff00", "#ff0000", "#0000ff", "#000000", "#ffffff", "#808080", "#16abbe"})
            bases.push_back(h);
        bool readable = true, button = true, kept_l = true;
        std::string worst;
        for (const auto& b : bases) {
            const double body   = contrast(T("#eef8fa", b), T("#0f2229", b));   // text on the window
            const double drawer = contrast(T("#eef8fa", b), T("#12313a", b));   // text on a drawer
            const double hero   = contrast(T("#053039", b), T("#99e9f2", b));   // Open… on its pill
            if (body < 12 || drawer < 10) { readable = false; worst += b + " "; }
            if (hero < 7) { button = false; worst += b + "(btn) "; }
            for (const char* lit : {"#0c4a57", "#0b6274", "#99e9f2", "#12313a", "#0f2229"}) {
                const double l0 = lum(*parse_hex(lit)), l1 = lum(T(lit, b));
                if (std::abs(l1 - l0) > 0.02 + 0.08 * l0) { kept_l = false; worst += b + ":" + lit + " "; }
            }
        }
        check("tint: every base keeps text on the window and drawers readable (>= 12:1 / 10:1)", readable, worst);
        check("tint: every base keeps Open… readable on its pill (>= 7:1)", button, worst);
        check("tint: lightness kept -- each colour's luminance stays put", kept_l, worst);

        const std::string blue = tint_css(css, "#228be6");
        check("tint: a tint changes the sheet", blue != css && blue.size() > css.size() / 2);
        check("tint: the error red is kept", blue.find("#ff8787") != std::string::npos &&
              blue.find("#ffe3e3") != std::string::npos);
        check("tint: greys and white pass through",
              blue.find("rgba(255,255,255,0.10)") != std::string::npos &&
              blue.find("rgba(0,0,0,0.22)") != std::string::npos && blue.find("#ffffff") != std::string::npos);
        check("tint: no teal accent left behind", blue.find("#99e9f2") == std::string::npos &&
              blue.find("153,233,242") == std::string::npos);
        {
            const auto acc = T("#99e9f2", "#228be6");
            check("tint: toward blue, the accent turns bluer", acc.b > acc.g && acc.b > acc.r);
            const auto rose = T("#99e9f2", "#e64980");
            check("tint: toward rose, the accent turns pink", rose.r > rose.g && rose.r > rose.b);
            const auto grey = T("#99e9f2", "#000000");
            check("tint: black (no hue) gives a grey skin",
                  std::abs(grey.r - grey.g) < 0.02 && std::abs(grey.g - grey.b) < 0.02);
        }
        {   // Every literal the turn writes is a real colour, for every base.
            static const std::regex rgba(R"(rgba\(([^)]*)\))");
            bool ok = true;
            for (const auto& b : bases) {
                const std::string t = tint_css(css, b);
                for (auto it = std::sregex_iterator(t.begin(), t.end(), rgba);
                     it != std::sregex_iterator(); ++it) {
                    int r, g, bb; double a;
                    if (std::sscanf((*it)[1].str().c_str(), "%d,%d,%d,%lf", &r, &g, &bb, &a) != 4 ||
                        r < 0 || r > 255 || g < 0 || g > 255 || bb < 0 || bb > 255) ok = false;
                }
            }
            check("tint: every rgba() written is in range, for every base", ok);
        }
        check("tint: the logo turns too (any text of colour literals)",
              tint_css("<stop stop-color=\"#15aabf\"/>", "#e64980") != "<stop stop-color=\"#15aabf\"/>");

        // The settings file.
        const auto dir = std::filesystem::temp_directory_path() / "lookit_selftest_tint";
        std::filesystem::remove_all(dir);
        const std::string f = (dir / "lookit" / "settings.json").string();
        check("tint: first run -- no file, teal", core::load_tint(f) == core::kDefaultTint);
        check("tint: save makes the folder and round-trips",
              core::save_tint(f, "#E64980") && core::load_tint(f) == "#e64980");
        check("tint: save refuses a non-colour and keeps the old", !core::save_tint(f, "blue") &&
              core::load_tint(f) == "#e64980");
        { std::ofstream o(f); o << "{\"tint\":\"#7950f2\",\"later\":42}"; }
        core::save_tint(f, "#2f9e44");
        { std::ifstream i(f); std::string all((std::istreambuf_iterator<char>(i)), {});
          check("tint: save keeps the file's other settings", all.find("\"later\": 42") != std::string::npos &&
                core::load_tint(f) == "#2f9e44"); }
        { std::ofstream o(f); o << "{not json"; }
        check("tint: a broken file -> teal, no throw", core::load_tint(f) == core::kDefaultTint);
        { std::ofstream o(f); o << "{\"tint\":\"plaid\"}"; }
        check("tint: a non-colour in the file -> teal", core::load_tint(f) == core::kDefaultTint);
        std::filesystem::remove_all(dir);
    }

    std::cout << "-----------------------------------------------\n";
    std::cout << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
