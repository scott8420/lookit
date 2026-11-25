#pragma once
#include "core/Marks.hpp"

#include <cairo.h>
#include <string>
#include <utility>

// MarkArt -- drawing a mark (core::Mark) with Cairo. ONE routine for every
// place a mark is drawn, so they can't disagree: the page view's live overlay
// (DocView), a page rendered for thumbnails and export (Document), and the
// overlay Save stamps into the PDF (Save). Signature pictures are PNGs Lookit
// wrote, each read once and kept. Text is Pango, metrics unhinted, so it wraps
// the same on screen, in a PNG and in the PDF.
namespace lookit::mark_art {

// A signature picture as a Cairo surface (owned by the cache; null if
// unreadable), and its size in pixels ({0, 0} when unreadable).
cairo_surface_t* surface(const std::string& path);
std::pair<int, int> size(const std::string& path);

// Turn `cr` so that drawing in a page's UNTURNED frame (bw x bh points) lands
// turned `turn` quarter turns clockwise -- the same turn as the page itself.
void turn_frame(cairo_t* cr, int turn, double bw, double bh);

// What to draw. Notes are icons on screen and in pictures, but in a saved PDF
// they are real sticky notes (annotations), so Save leaves them out here.
enum class Notes { Draw, Skip };
// Highlights: multiplied into the page where we draw onto it (screen,
// thumbnails, export); a saved PDF gets the same colour see-through instead.
enum class Blend { Multiply, Translucent };

// Draw `m` into `cr`, whose user space is the page's UNTURNED frame in points.
void draw(cairo_t* cr, const core::Mark& m, Notes notes = Notes::Draw, Blend blend = Blend::Multiply);

// A text box's height for its width (its own upright frame), in points.
double text_height(const core::Mark& m);

}  // namespace lookit::mark_art
