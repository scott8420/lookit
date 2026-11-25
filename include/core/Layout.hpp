#pragma once
#include <utility>
#include <vector>

// core/Layout -- where each page sits in the view, and the zoom arithmetic.
// GTK-free: the view hands in page sizes (PDF points or image pixels) and the
// viewport, and gets back rectangles in view pixels. Everything the view needs
// to be right about -- fit width, fit page, which pages are on screen, which
// page "you are on", the zoom steps -- lives here, so the selftest can hold it.
namespace lookit::core::layout {

struct Size { double w = 0, h = 0; };
struct Rect { double x = 0, y = 0, w = 0, h = 0; bool operator==(const Rect&) const = default; };

// How pages are arranged.
//   Continuous -- one column, top to bottom, centred.
//   Book       -- spreads like a printed book: page 1 alone on the RIGHT (the
//                 cover / recto), then 2|3, 4|5 ... facing across the spine;
//                 an even last page sits alone on the left.
enum class Mode { Continuous, Book };

// One row of the arrangement: the page on the left of the spine and the one
// on the right (-1 = none). Continuous rows put their one page in `left`.
struct Row { int left = -1, right = -1; };
std::vector<Row> rows(int count, Mode mode);
int row_of(int page, Mode mode);           // which row page sits in
int first_of_row(int row, Mode mode);      // the lowest page in that row

// The first page of the row before / after page's row (Left / Right arrows,
// next / previous page). Clamped to [0, count-1].
int step(int page, int dir, int count, Mode mode);

// The pages placed in view pixels, `gap` apart, `margin` all round.
struct Placed {
    std::vector<Rect> pages;
    double width = 0;    // content width  (>= viewport width)
    double height = 0;   // content height
};

inline constexpr double kGap = 16;
inline constexpr double kMargin = 24;
inline constexpr double kMinScale = 0.10;
inline constexpr double kMaxScale = 8.0;

inline constexpr double kSpine = 2;   // Book: the gap between facing pages

Placed place(const std::vector<Size>& pages, double scale, double viewport_w,
             Mode mode = Mode::Continuous, double gap = kGap, double margin = kMargin);

// PAGED view (the default, Scott s005): only the row holding `page` -- one
// page, or a facing pair in Book mode -- is placed; it is centred in the
// viewport when it fits and starts at the margin when zoomed past it. Every
// other page gets a zero rect far above the view, so visible() never sees it.
Placed place_row(const std::vector<Size>& pages, int page, double scale, double viewport_w,
                 double viewport_h, Mode mode = Mode::Continuous, double margin = kMargin);

// Pages touching [y0, y1): {first, last} inclusive; {-1, -1} when none.
// (Every page in between is drawn; in Book mode a row's two pages may
// differ in height, so this scans rather than bisects.)
std::pair<int, int> visible(const Placed& p, double y0, double y1);

// The page "you are on": the one under y (the viewport's middle), else the
// nearest. -1 for an empty document.
int page_at(const Placed& p, double y);

// The scale at which the WIDEST row (a page, or a spread) fills the width.
double fit_width(const std::vector<Size>& pages, double viewport_w,
                 Mode mode = Mode::Continuous, double margin = kMargin);
// The scale at which the row holding page `i` fits whole in the viewport.
double fit_page(const std::vector<Size>& pages, int i, double viewport_w, double viewport_h,
                Mode mode = Mode::Continuous, double margin = kMargin);

// The next step up / down the zoom ladder (25% ... 800%), from any scale.
double zoom_in(double scale);
double zoom_out(double scale);
double clamp_scale(double scale);

}  // namespace lookit::core::layout
