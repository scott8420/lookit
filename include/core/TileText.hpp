#pragma once
#include <cstddef>
#include <string>

// core/TileText -- the QUICK-SETTINGS-TILE stone's GTK-free half.
//
// The tile (extension/<app>-tile@.../) hands text to the app over D-Bus:
//   org.freedesktop.Application.ActivateAction("receive-text", [text])
// The shell side cleans and caps it in lib.js before sending; the app cleans
// and caps it AGAIN here on arrival, because the same action is also reached
// from the command line (`lookit --send TEXT`) and from `gapplication action`,
// which never pass through lib.js. Same rules both sides -- the selftest and
// tools/test_extension_lib.mjs check the same cases, so the two can't drift
// unnoticed. (docs/quick-settings-tile.md)
namespace lookit::core::tile {

// What the tile accepts at once. A capture is a few lines, not a chapter.
// Counted in Unicode code points (lib.js counts UTF-16 units; the two agree
// for everything outside the astral planes -- close enough for a cap).
inline constexpr std::size_t kMaxChars = 20000;

// Where the text came from: picks the message when there is none.
enum class Where { Line, Clipboard, Selection };

// Line endings made plain (CRLF / CR -> LF), trailing blanks off each line,
// runs of 3+ newlines squeezed to one blank line, ends trimmed.
std::string cleaned(const std::string& raw);

// What to do with it: text to take, or an error to show, or neither (an empty
// line is just nothing -- no complaint).
struct Request {
    std::string text;    // non-empty: take this
    std::string error;   // non-empty: say this instead
};
Request request(const std::string& raw, Where where = Where::Line);

std::size_t codepoints(const std::string& utf8);

}  // namespace lookit::core::tile
