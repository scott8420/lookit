#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// core/ThumbSize -- how big the thumbnails in View's sidebar are. (s015.)
//
// Three sizes: Small, Medium (the s004 size, the default), Large. The size sets
// each thumbnail's width and so the sidebar's width; in Book View the strip
// shows the pages in the same facing pairs as the view (core::layout::rows),
// and each page there is a little narrower so a pair doesn't double the strip.
//
// Remembered in ~/.config/lookit/settings.json as "thumb_size": "small" |
// "medium" | "large", next to the base colour. GTK-free; the UI resolves the
// XDG path and hands it in (as core/Tint does).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

enum class ThumbSize { Small, Medium, Large };
inline constexpr ThumbSize kDefaultThumbSize = ThumbSize::Medium;

// One page's thumbnail width in logical px. book = shown in facing pairs.
int thumb_width(ThumbSize s, bool book);
// The gap between the two pages of a pair (the strip's spine).
inline constexpr int kThumbSpine = 4;
// The sidebar's width: one row of thumbnails plus the row padding.
int thumb_strip_width(ThumbSize s, bool book);

const char* thumb_size_id(ThumbSize s);                // "small" / "medium" / "large"
bool        parse_thumb_size(const std::string& id, ThumbSize& out);

// load: missing, unreadable or unknown -> Medium. save keeps the other keys.
ThumbSize load_thumb_size(const std::string& file);
bool      save_thumb_size(const std::string& file, ThumbSize s);

}  // namespace lookit::core
