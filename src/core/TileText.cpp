#include "core/TileText.hpp"

// The C++ twin of extension/lookit-tile@scott8420.github.io/lib.js. Change one,
// change the other, and both tests (selftest + tools/test_extension_lib.mjs).

namespace lookit::core::tile {

std::size_t codepoints(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;   // count lead bytes only
    return n;
}

std::string cleaned(const std::string& raw) {
    // 1. CRLF / CR -> LF
    std::string a;
    a.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            a += '\n';
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
        } else {
            a += raw[i];
        }
    }
    // 2. trailing spaces / tabs off each line
    //    A run of blanks is held back until the next character says whether
    //    it was trailing (a newline: drop it) or inside the line (keep it).
    std::string b;
    b.reserve(a.size());
    std::string run;
    for (char c : a) {
        if (c == ' ' || c == '\t') { run += c; continue; }
        if (c != '\n') b += run;   // blanks inside a line stay as they were
        run.clear();
        b += c;
    }
    b += run;   // trailing blanks at the very end: trimmed in step 4
    // 3. 3+ newlines -> 2
    std::string d;
    d.reserve(b.size());
    std::size_t nl = 0;
    for (char c : b) {
        if (c == '\n') { if (++nl <= 2) d += c; continue; }
        nl = 0;
        d += c;
    }
    // 4. trim ends (JS String.trim: whitespace incl. newlines)
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f'; };
    std::size_t lo = 0, hi = d.size();
    while (lo < hi && ws(d[lo])) ++lo;
    while (hi > lo && ws(d[hi - 1])) --hi;
    return d.substr(lo, hi - lo);
}

Request request(const std::string& raw, Where where) {
    Request r;
    std::string text = cleaned(raw);
    if (text.empty()) {
        if (where == Where::Clipboard) r.error = "The clipboard has no text.";
        else if (where == Where::Selection) r.error = "Nothing is selected.";
        return r;
    }
    const std::size_t n = codepoints(text);
    if (n > kMaxChars) {
        r.error = "That's " + std::to_string(n) + " characters -- the tile takes up to " +
                  std::to_string(kMaxChars) + " at a time.";
        return r;
    }
    r.text = std::move(text);
    return r;
}

}  // namespace lookit::core::tile
