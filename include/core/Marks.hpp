#pragma once
#include "core/Layout.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Marks -- what's put ON a page: a signature (s008), and the markup of
// s009 -- highlights, sticky notes, boxes, ellipses, lines, arrows, text.
// GTK-free.
//
// A MARK lives INSIDE its page's record (core::PageRec::marks), so every page
// verb already carries it: a turned page turns its marks, a moved page takes
// them along, a duplicated page is a marked page, a deleted page's marks come
// back with it on undo. Adding, moving, sizing, recolouring, editing and
// removing a mark are changes to that one record through the one door
// (core::pages).
//
// Where it sits is in the page's UNTURNED frame -- PDF points, origin top-left,
// as the page came from its file (poppler's frame) -- so turning the page
// never moves it on the paper. The view works in the TURNED frame (what's on
// screen); the functions below cross between the two.
//
// Geometry by kind:
//   box kinds (signature, box, ellipse, text, note): the box x, y, w, h.
//   line kinds (line, arrow): start (x, y), end (x + w, y + h) -- w / h may be
//     negative; the arrow's head is at the end.
//   highlight: `parts`, one rectangle per line of text; x, y, w, h is their
//     union (what's hit, framed, moved).
// `turn` is the mark's own quarter turns in the unturned frame: a signature or
// text put on a page you see turned is turned back, so it reads upright to you.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

enum class MarkKind { Signature, Highlight, Note, Box, Ellipse, Line, Arrow, Text };

struct Mark {
    MarkKind      kind = MarkKind::Signature;
    std::string   art;                  // signature: the picture's path (a PNG Lookit wrote)
    double        x = 0, y = 0;         // see the kinds above; unturned page points
    double        w = 0, h = 0;
    int           turn = 0;             // own quarter turns clockwise in that frame
    std::uint32_t color = 0x000000ffu;  // 0xRRGGBBAA
    double        width = 2;            // stroke, points (box, ellipse, line, arrow)
    std::string   text;                 // note, text box
    double        size = 14;            // text, points
    std::vector<layout::Rect> parts;    // highlight
    bool operator==(const Mark&) const = default;
};

namespace marks {

using layout::Rect;
struct Point { double x = 0, y = 0; bool operator==(const Point&) const = default; };

const char* noun(MarkKind k);             // "signature", "highlight", ... (undo labels)
bool is_line(MarkKind k);                 // line, arrow
bool resizable(MarkKind k);               // not a highlight, not a note
bool upright(MarkKind k);                 // turned back to read upright: signature, text, note

// The colours offered (0xRRGGBBAA), and each kind's default.
const std::vector<std::uint32_t>& palette();
std::uint32_t default_color(MarkKind k);

// ── crossing frames. `w` x `h` is the UNTURNED page; `t` the page's turn ──
Point turn_point(Point p, double w, double h, int t);     // unturned -> seen
Point unturn_point(Point p, double w, double h, int t);   // seen -> unturned
Point unturn_vector(Point v, int t);                      // a drag, seen -> unturned
Rect  turn_rect(const Rect& r, double w, double h, int t);
Rect  unturn_rect(const Rect& r, double w, double h, int t);

// The mark as seen: its box (lines: the box round both ends) ...
Rect view_box(const Mark& m, double w, double h, int t);
// ... a line's two ends ...
std::pair<Point, Point> view_line(const Mark& m, double w, double h, int t);
// ... a highlight's parts ...
std::vector<Rect> view_parts(const Mark& m, double w, double h, int t);
// ... and how its own picture / text is turned on screen.
int view_turn(const Mark& m, int t);

// Changed from the seen frame (everything else kept):
Mark with_view_box(Mark m, const Rect& seen, double w, double h, int t);           // box kinds
Mark with_view_line(Mark m, Point a, Point b, double w, double h, int t);          // line kinds
Mark moved(Mark m, double dx_seen, double dy_seen, int t);                          // any kind

// ── making marks ──
// A signature of `art` (art_w x art_h pixels): upright to the reader, a third
// of the seen width wide (never taller than a sixth of the seen height),
// centred, two thirds down.
Mark signature(const std::string& art, double art_w, double art_h, double w, double h, int t);
// A box kind from a dragged rectangle (seen frame), upright to the reader.
Mark boxed(MarkKind k, const Rect& seen, std::uint32_t color, double width, double w, double h, int t);
// A line kind from a drag (seen frame).
Mark line(MarkKind k, Point a, Point b, std::uint32_t color, double width, double w, double h, int t);
// A highlight from its parts (seen frame); none -> the dragged box itself.
Mark highlight(const std::vector<Rect>& seen_parts, const Rect& dragged, std::uint32_t color,
               double w, double h, int t);
// A note at a click (seen frame): a fixed-size icon, its corner at the click.
inline constexpr double kNoteSize = 20;
Mark note(Point at, const std::string& text, std::uint32_t color, double w, double h, int t);

// The rectangle two points span.
Rect span(Point a, Point b);

// ── hitting marks on screen ──
enum class Part { None, Body, Corner, Start, End };
struct Shape {          // one mark as the view sees it, in view pixels
    Rect  box;
    bool  line = false;
    Point a, b;         // line ends
    bool  resizable = true;
};
struct Hit { int index = -1; Part part = Part::None; };
// Topmost (last) first. A line is hit within `tol` of its segment, its ends
// are handles; a box's corner is the resize handle (if resizable).
Hit hit(const std::vector<Shape>& shapes, double x, double y, double tol);

// ── saving ──
// A point in the seen-before-our-turns frame (poppler's: the crop box, top-
// left origin, the file's own /Rotate applied) -> PDF user space.
Point to_pdf(Point p, double llx, double lly, double urx, double ury, int rotate_deg);

}  // namespace marks

}  // namespace lookit::core
