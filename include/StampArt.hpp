#pragma once
#include "core/Sign.hpp"

#include <cairo.h>
#include <string>

// StampArt -- drawing a signature (core::Stamp) with Cairo. One routine for the
// three places a stamp is drawn, so they can't disagree: a page rendered for
// thumbnails and export (Document), the overlay Save stamps into the PDF
// (Save), and the page view's live overlay sizes come from the same box.
// The pictures are PNGs Lookit wrote; each is read once and kept.
namespace lookit::stamp_art {

// The picture as a Cairo surface (owned by the cache; null if unreadable).
cairo_surface_t* surface(const std::string& path);
// Its size in pixels; {0, 0} when unreadable.
std::pair<int, int> size(const std::string& path);

// Draw `s` into `cr`, whose user space is the page's UNTURNED frame in points.
void draw(cairo_t* cr, const core::Stamp& s);

}  // namespace lookit::stamp_art
