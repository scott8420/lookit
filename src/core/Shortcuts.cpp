#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace lookit::core {

// ─────────────────────────────────────────────────────────────────────────────
// Accel formatting
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += char(std::tolower((unsigned char)c));
    return o;
}

// GTK key name -> display glyph. Single alpha keys uppercase; named keys map to a
// glyph where one reads better, otherwise the name is prettified.
std::string map_key(const std::string& key) {
    if (key.empty()) return "";
    static const std::map<std::string, std::string> named = {
        {"comma", ","},     {"period", "."},     {"question", "?"},
        {"slash", "/"},     {"minus", "\u2212"}, {"plus", "+"},
        {"equal", "="},     {"space", "Space"},
        {"Return", "Enter"},{"Escape", "Esc"},
        {"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"},
        {"Delete", "Del"},  {"BackSpace", "Backspace"},
        {"Page_Up", "Page Up"}, {"Page_Down", "Page Down"},
        {"KP_Add", "Keypad +"}, {"KP_Subtract", "Keypad \u2212"}, {"KP_0", "Keypad 0"},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;
    if (key.size() == 1) {
        std::string s = key;
        s[0] = char(std::toupper((unsigned char)s[0]));
        return s;
    }
    std::string s = key;
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// Split an accel into its modifier flags + bare key name. Shared by
// format_accel and any future consumer so they can't disagree.
struct Parsed { bool ctrl = false, alt = false, shift = false, super = false; std::string key; };

Parsed parse_accel(const std::string& accel) {
    Parsed p;
    std::size_t i = 0;
    while (i < accel.size()) {
        if (accel[i] == '<') {
            std::size_t j = accel.find('>', i);
            if (j == std::string::npos) break;
            const std::string ml = lower(accel.substr(i + 1, j - i - 1));
            if (ml == "ctrl" || ml == "control" || ml == "primary") p.ctrl = true;
            else if (ml == "alt" || ml == "mod1")                   p.alt = true;
            else if (ml == "shift")                                 p.shift = true;
            else if (ml == "super" || ml == "meta" || ml == "mod4") p.super = true;
            i = j + 1;
        } else {
            p.key = accel.substr(i);
            break;
        }
    }
    return p;
}

}  // namespace

std::string format_accel(const std::string& accel) {
    const Parsed p = parse_accel(accel);
    const std::string k = map_key(p.key);
    std::string out;
    auto add = [&](const std::string& s) {
        if (!out.empty()) out += "+";
        out += s;
    };
    if (p.ctrl)  add("Ctrl");
    if (p.alt)   add("Alt");
    if (p.shift) add("Shift");
    if (p.super) add("Super");
    if (!k.empty()) add(k);
    return out;
}

std::string ShortcutSpec::display_keys() const {
    if (!keys.empty()) return keys;
    std::string out;
    for (const auto& a : accels) {
        if (!out.empty()) out += "  /  ";
        out += format_accel(a);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// The registry  (authored section [A-Z] -> row)
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ShortcutSpec>& shortcut_registry() {
    // Fields: {section, action, accels, keys, description}
    // action non-empty => wired via set_accels_for_action. keys empty => derived.
    // Deliberately small -- a seed proves the SHAPE, not a product's key set. It
    // carries one doc-only row so that half of the paradigm is exercised too.
    static const std::vector<ShortcutSpec> kReg = {
        // ── Drawers ───────────────────────────────────────────────────────────
        {"Drawers", "win.toggle-left-drawer",  {"F9"},        "", "Help drawer (left) -- open / close"},
        {"Drawers", "win.toggle-right-drawer", {"<Shift>F9"}, "", "Info drawer (right) -- open / close"},
        {"Drawers", "win.sidebar",             {"F8"},        "", "Sidebar (Home / View) -- show / hide, for a full view"},
        // Doc-only: Esc is heard by the window, and only while a drawer is open.
        {"Drawers", "", {}, "Esc", "Close the open drawer"},

        // ── Edit (page tools) ─────────────────────────────────────────────────
        // Doc-only: the window hears these, never taking them from a text box.
        {"Edit", "", {}, "Ctrl+Z", "Undo -- anywhere but a text box"},
        {"Edit", "", {}, "Ctrl+Shift+Z  /  Ctrl+Y", "Redo"},
        {"Edit", "", {}, "Delete", "Delete the selected mark (signature, note, shape...), else the page you're on"},
        {"Edit", "", {}, "Drag a thumbnail", "Move the page; drop files between thumbnails to insert them"},
        {"Edit", "win.rotate-left",  {"<Ctrl>l"}, "", "Rotate left"},
        {"Edit", "win.rotate-right", {"<Ctrl>r"}, "", "Rotate right"},
        {"Edit", "win.duplicate-page", {"<Ctrl>d"}, "", "Duplicate the page"},
        {"Edit", "win.insert-file",  {"<Ctrl>i"}, "", "Insert pages from a file (PDF or picture)"},
        {"Edit", "win.extract",      {"<Ctrl>e"}, "", "Extract pages to a new PDF"},
        {"Edit", "win.save",         {"<Ctrl>s"}, "", "Save"},
        {"Edit", "win.save-as",      {"<Ctrl><Shift>s"}, "", "Save as"},
        {"Edit", "win.sign",         {"<Ctrl><Shift>i"}, "", "Add your signature to the page (draw it the first time)"},
        {"Edit", "win.markup",       {"<Ctrl><Shift>a"}, "", "Markup bar -- highlight, note, box, ellipse, line, arrow, text"},
        {"Edit", "win.export",       {"<Ctrl><Shift>e"}, "", "Export -- pages as PNG / JPEG / TIFF, the text, a picture as another format"},

        // ── Forms (s011) ──────────────────────────────────────────────────────
        // Doc-only: a click on a field opens it; the keys are the popover's.
        {"Forms", "", {}, "Click a field", "Tick a box, pick a choice, or type in it"},
        {"Forms", "", {}, "Enter  /  Ctrl+Enter", "Keep what you typed (one line / several)"},
        {"Forms", "", {}, "Tab  /  Shift+Tab", "Keep it and go to the next / previous field"},
        {"Forms", "", {}, "Esc", "Leave the field as it was"},
        {"Forms", "win.password", {}, "Pages menu", "Password -- add, change or remove; Save writes it"},

        // ── General ───────────────────────────────────────────────────────────
        {"General", "win.open",          {"<Ctrl>o"}, "", "Open a PDF or a picture"},
        {"General", "win.shortcuts",     {"<Ctrl>question", "<Ctrl>slash"}, "",
         "Keyboard shortcuts (this window)"},
        {"General", "win.about",         {"F1"}, "", "About Lookit"},
        {"General", "win.quit",          {"<Ctrl>q", "<Ctrl>w"}, "", "Close the window"},

        // ── Mouse ─────────────────────────────────────────────────────────────
        // Doc-only: gestures are not GActions, but the reader still needs them.
        {"Mouse", "", {}, "Ctrl+wheel  /  pinch", "Zoom about the pointer"},
        {"Mouse", "", {}, "Drop a file", "Open it"},
        {"Mouse", "", {}, "Drag a mark  /  its corner or ends", "Move it / size it (Esc puts it back mid-drag)"},
        {"Mouse", "", {}, "Double-click a note or text", "Edit what it says"},
        {"Mouse", "", {}, "Esc with a drawing tool", "Back to Select"},

        // ── Pictures (s010) ───────────────────────────────────────────────────
        {"Pictures", "", {}, "\u2190  /  \u2192", "Previous / next picture in the folder"},
        {"Pictures", "", {}, "Ctrl+L  /  Ctrl+R", "Rotate left / right"},
        {"Pictures", "win.flip-horizontal", {"<Ctrl><Shift>h"}, "", "Flip left-right"},
        {"Pictures", "win.flip-vertical",   {"<Ctrl><Shift>v"}, "", "Flip upside down"},
        {"Pictures", "win.crop",            {"<Ctrl><Shift>x"}, "", "Crop -- drag a frame; Enter crops, Esc cancels"},
        {"Pictures", "win.resize",          {"<Ctrl><Shift>r"}, "", "Resize"},
        {"Pictures", "win.light",           {"<Ctrl><Shift>l"}, "", "Light -- brightness, contrast, saturation"},
        {"Pictures", "", {}, "Ctrl+S  /  Ctrl+Shift+S", "Save over the file / save as PNG, JPEG, TIFF, BMP or PDF"},

        // ── Reading ───────────────────────────────────────────────────────────
        // Doc-only: the page view hears these itself (DocView), so they never
        // leave a text box without its own keys.
        {"Reading", "", {}, "Space  /  Shift+Space", "Down / up a screen; past the edge, turn the page"},
        {"Reading", "", {}, "Page Down  /  Page Up", "Down / up a screen"},
        {"Reading", "", {}, "\u2190  /  \u2192", "Previous / next page (spread, in Book view)"},
        {"Reading", "", {}, "Home  /  End", "First / last page"},
        {"Reading", "win.go-to-page",    {"<Ctrl>g"}, "", "Go to a page"},
        {"Reading", "win.find",          {"<Ctrl>f"}, "", "Find in the document"},
        {"Reading", "", {}, "Enter  /  Shift+Enter", "Next / previous match (in Find)"},
        {"Reading", "win.thumbs",        {"<Ctrl>t"}, "", "Thumbnails -- show / hide"},
        {"Reading", "win.book",          {"<Ctrl>b"}, "", "Book view -- spreads, page 1 alone on the right"},

        // ── Zoom ──────────────────────────────────────────────────────────────
        {"Zoom", "win.zoom-in",     {"<Ctrl>equal", "<Ctrl>plus", "<Ctrl>KP_Add"}, "", "Zoom in"},
        {"Zoom", "win.zoom-out",    {"<Ctrl>minus", "<Ctrl>KP_Subtract"}, "", "Zoom out"},
        {"Zoom", "win.zoom-actual", {"<Ctrl>0", "<Ctrl>KP_0"}, "", "Actual size (100%)"},
        {"Zoom", "win.fit-width",   {"<Ctrl>1"}, "", "Fit width"},
        {"Zoom", "win.fit-page",    {"<Ctrl>9", "<Ctrl>2"}, "", "Fit window -- the whole page (or pair)"},
    };
    return kReg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision detection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> find_accel_collisions() {
    std::map<std::string, std::pair<int, bool>> seen;  // accel -> {count, any_action}
    for (const auto& s : shortcut_registry())
        for (const auto& a : s.accels) {
            auto& e = seen[a];
            e.first += 1;
            if (!s.action.empty()) e.second = true;
        }
    std::vector<std::string> out;
    for (const auto& [accel, e] : seen)
        if (e.first >= 2 && e.second) out.push_back(accel);  // std::map keeps it sorted+unique
    return out;
}

}  // namespace lookit::core
