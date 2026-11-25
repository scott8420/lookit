#pragma once
#include "core/Layout.hpp"

#include <vector>

// core/Search -- find-in-document's arithmetic, GTK-free. The backend (poppler)
// produces hits page by page; this orders them, steps through them (wrapping),
// says where to start from the page you're on, and turns poppler's rectangles
// (PDF points, origin BOTTOM-left) into page rectangles (origin top-left).
namespace lookit::core::search {

struct Hit {
    int page = 0;
    layout::Rect r;   // page units (points), origin top-left
};

// Poppler's find rectangle -> top-left origin, on a page `page_h` tall.
layout::Rect from_pdf(double x1, double y1, double x2, double y2, double page_h);

// A rect on an unturned page (w x h) as it lands after `turn` quarter turns
// clockwise -- hits on a rotated page.
layout::Rect turn_rect(const layout::Rect& r, double w, double h, int turn);

// Hits sorted reading-order: page, then top to bottom, then left to right.
void sort(std::vector<Hit>& hits);

// The next / previous hit from `cur`, wrapping. -1 when there are none.
int step(const std::vector<Hit>& hits, int cur, int dir);

// The first hit on or after `page` (wrapping to the start). -1 when none.
int first_from(const std::vector<Hit>& hits, int page);

}  // namespace lookit::core::search
