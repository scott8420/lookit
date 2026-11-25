#pragma once
#include "core/Layout.hpp"
#include "core/Marks.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Sign -- box arithmetic for dragging marks (core/Marks), the ink that
// turns a scan of a signature into a picture, and the export arithmetic.
// GTK-free.
//
// move / resize keep a box on its page (the seen frame, page points); resize
// keeps the aspect for a signature and lets a shape go free. ink() makes a scan
// into ink on clear (white paper -> transparent).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

namespace sign {

using layout::Rect;

// Drag the box by (dx, dy) page points, kept on a page `pw` x `ph`.
Rect move(const Rect& start, double dx, double dy, double pw, double ph);
// Drag the bottom-right corner by (dx, dy): the aspect is kept, the top-left
// stays, the box stays on the page and no smaller than `min_h` tall.
Rect resize(const Rect& start, double dx, double dy, double pw, double ph, double min_h = 12);
// The same, free: each side follows the pointer, at least `min` long.
Rect resize_free(const Rect& start, double dx, double dy, double pw, double ph, double min = 6);
// A point kept on the page.
std::pair<double, double> clamp_point(double x, double y, double pw, double ph);

// A picture of a signature as ink: RGBA (straight alpha, 8 bits) in place.
// Light paper becomes clear, ink keeps its colour, the ramp between is soft so
// the edges stay smooth. Pixels already clear stay clear.
void ink(std::uint8_t* rgba, int w, int h, int stride);
// The smallest box holding every pixel with alpha above `threshold`;
// w == 0 when there is none.
Rect ink_bounds(const std::uint8_t* rgba, int w, int h, int stride, int threshold = 8);

}  // namespace sign

// ─────────────────────────────────────────────────────────────────────────────
// Export (convert) -- the arithmetic, GTK-free.
// ─────────────────────────────────────────────────────────────────────────────
namespace convert {

// Cairo's ARGB32 (premultiplied, native-endian 32-bit) -> RGBA bytes (straight
// alpha). `flatten`: onto white, alpha 255 (JPEG, BMP: no alpha channel).
void argb32_to_rgba(const std::uint8_t* src, int src_stride, int w, int h, std::uint8_t* dst,
                    int dst_stride, bool flatten);

// "Report-p03.png": the page number padded to the count's width; a single
// page has no number.
std::string page_name(const std::string& stem, int page, int count, const std::string& ext);
// `name` if `exists` says it's free, else "stem (2).ext", "stem (3).ext"...
std::string unique_name(const std::string& name, const std::function<bool(const std::string&)>& exists);

// Pixels for a page `w_pt` x `h_pt` points at `dpi`.
std::pair<int, int> pixels(double w_pt, double h_pt, int dpi);

}  // namespace convert

}  // namespace lookit::core
