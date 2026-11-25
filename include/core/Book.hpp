#pragma once
#include "core/Layout.hpp"
#include "core/Zip.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Book -- what Lookit needs to know about books and comics (s012),
// GTK-free so the selftest holds it.
//
//   comic -- a CBZ is a ZIP of pictures: its pages are the pictures, in
//            natural name order ("page2" before "page10"), Mac and hidden
//            leftovers skipped. A page's size comes from the picture's header
//            (PNG, JPEG, GIF, BMP, WebP), so a 200-page comic opens without
//            decoding 200 pictures.
//   epub  -- an EPUB is a ZIP of XHTML: container.xml names the package
//            (.opf); its spine is the reading order and its manifest names
//            the cover. Each chapter becomes blocks of plain text (headings
//            and paragraphs); the UI lays them out into pages with Pango.
//            No CSS, no pictures inside chapters -- a "look inside", not a
//            reading app.
//   words -- DjVu's text layer is words with boxes; find_words turns a
//            search into boxes (a phrase may run over several words).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

namespace comic {
bool is_picture_name(const std::string& name);
// Indices into `entries` of the comic's pages, in reading order.
std::vector<int> pages(const std::vector<zip::Entry>& entries);
// Width and height from a picture's first bytes; nullopt for a kind it
// doesn't know (the caller decodes it instead).
std::optional<std::pair<int, int>> image_size(const std::string& head);
}  // namespace comic

namespace epub {
// A path inside the archive: `base`'s folder + `href`, "../" and "./" folded,
// "#part" dropped, %20 and friends decoded.
std::string resolve(const std::string& base, const std::string& href);
// META-INF/container.xml -> the package's path ("" when none).
std::string package_path(const std::string& container_xml);

struct Package {
    std::string title;
    std::vector<std::string> spine;   // chapter paths, reading order
    std::string cover;                // the cover picture's path, or ""
};
Package parse_package(const std::string& opf_xml, const std::string& opf_path);

struct Block {
    bool heading = false;
    std::string text;                 // UTF-8, whitespace folded
    bool operator==(const Block&) const = default;
};
// A chapter's XHTML -> its blocks (head, script and style skipped; entities
// decoded; block elements and <br> break paragraphs).
std::vector<Block> blocks(const std::string& xhtml);
}  // namespace epub

namespace words {
struct Word {
    std::string  text;
    layout::Rect box;                 // page units, origin top-left
};
// Every place `query` occurs (case-blind for ASCII), as the union box of the
// words it covers, one per line it spans. Words are in reading order;
// a phrase's words are matched with single spaces between them.
std::vector<layout::Rect> find(const std::vector<Word>& words, const std::string& query);
}  // namespace words

}  // namespace lookit::core
