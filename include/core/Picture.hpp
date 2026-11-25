#pragma once
#include "core/Layout.hpp"
#include "core/Undo.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Picture -- the picture tools (s010): rotate / flip, crop, resize, light,
// as ONE record the picture journal holds, plus the folder browsing arithmetic.
// GTK-free; the pixels themselves are drawn by PictureArt (app side), except
// the light pass, which is plain bytes and lives here so the selftest holds it.
//
// An edit never touches the file until Save. The record says how to make what
// you see from the picture as it came from the file, in this order:
//   1. ORIENT  -- mirror left-right (flip), then `turn` quarter turns clockwise.
//   2. CROP    -- a rectangle in the oriented frame (w == 0: the whole picture).
//   3. RESIZE  -- to out_w x out_h pixels (0: the crop's own size).
//   4. LIGHT   -- brightness, contrast, saturation, each -100..100 (0: as is).
// A verb is a change to that one record through the one door (Journal::run),
// so every verb is one undo step and the stray gate guards it like the pages.
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

struct PicEdit {
    int          turn = 0;          // quarter turns clockwise, 0..3 (after the flip)
    bool         flip = false;      // mirrored left-right, first
    layout::Rect crop;              // oriented frame, whole pixels; w == 0: none
    int          out_w = 0, out_h = 0;   // resized to; 0: the crop's size
    int          brightness = 0, contrast = 0, saturation = 0;   // -100..100
    bool operator==(const PicEdit&) const = default;
};

namespace picture {

using layout::Rect;
struct Pt { double x = 0, y = 0; bool operator==(const Pt&) const = default; };

// ── what the record means, for a picture `w` x `h` as it came from the file ──
std::pair<int, int> oriented(const PicEdit& e, int w, int h);   // the frame after step 1
Rect crop_of(const PicEdit& e, int w, int h);                   // step 2 (whole if none)
std::pair<int, int> out_size(const PicEdit& e, int w, int h);   // what you see, pixels
Pt   orient_point(Pt p, const PicEdit& e, int w, int h);        // file pixel -> oriented
bool lit(const PicEdit& e);                                     // any light set
bool unchanged(const PicEdit& e);                               // nothing at all

// ── the edits, as new records (the verbs below use these) ──
PicEdit turned(PicEdit e, int quarter, int w, int h);           // +1 right, -1 left
PicEdit flipped(PicEdit e, bool horizontal, int w, int h);      // mirror what you see
// Crop to `seen`: a rectangle in what you see now (out_size pixels).
PicEdit cropped(PicEdit e, const Rect& seen, int w, int h);
PicEdit resized(PicEdit e, int out_w, int out_h, int w, int h);
PicEdit with_light(PicEdit e, int brightness, int contrast, int saturation);
// The other side, kept in proportion: round(side_new * other / side).
int keep_aspect(int side_new, int side, int other);

// ── the light pass: Cairo ARGB32 (premultiplied, native-endian 32-bit) in place ──
void light_argb32(std::uint8_t* data, int w, int h, int stride, int brightness, int contrast, int saturation);

// ── the crop frame on screen (seen pixels) ──
enum class Grip { None, Body, TopLeft, TopRight, BottomLeft, BottomRight };
Grip crop_hit(const Rect& frame, double x, double y, double tol);
// Drag `start` by (dx, dy) holding `g`: a corner sizes (the opposite corner
// stays), the body moves; the frame stays inside `pw` x `ph`, at least `min`.
Rect crop_drag(const Rect& start, Grip g, double dx, double dy, double pw, double ph, double min = 8);

// ── browsing the folder ──
// "img2" before "img10", case-blind, then byte order to break ties.
bool natural_less(const std::string& a, const std::string& b);
// `names` sorted naturally; where `current` is (-1 if absent) and the name
// `dir` steps away ("" past either end -- browsing doesn't wrap).
std::vector<std::string> sorted(std::vector<std::string> names);
int index_of(const std::vector<std::string>& sorted_names, const std::string& current);
std::string step(const std::vector<std::string>& sorted_names, const std::string& current, int dir);

}  // namespace picture

// ── the picture journal's store: one record (id 0), the picture's edit ──────
class PicStore {
public:
    using Id    = int;
    using Value = PicEdit;

    std::optional<PicEdit> get(const Id& id) const;
    void put(const Id& id, const std::optional<PicEdit>& v);

    const PicEdit& edit() const { return m_edit; }
    void load(int w, int h);          // a picture opened: as it came, not a step
    int  w() const { return m_w; }    // the picture as it came from the file
    int  h() const { return m_h; }

    void set_door(std::function<bool()> open) { m_door = std::move(open); }
    int  strays() const { return m_strays; }

private:
    PicEdit               m_edit;
    int                   m_w = 0, m_h = 0;
    int                   m_strays = 0;
    std::function<bool()> m_door;
};

using PicJournal = Journal<PicStore>;

// The verbs. Each is one step; false (and nothing recorded) when it would
// change nothing or is refused (a crop smaller than a pixel, a size of 0).
namespace pic {
bool rotate(PicJournal& j, PicStore& s, int quarter);                       // "Rotate Left / Right"
bool flip(PicJournal& j, PicStore& s, bool horizontal);                     // "Flip Horizontally / Vertically"
bool crop(PicJournal& j, PicStore& s, const layout::Rect& seen);            // "Crop"
bool resize(PicJournal& j, PicStore& s, int out_w, int out_h);              // "Resize"
// Sliders: steps with the same `merge_key` inside the merge window are one.
bool light(PicJournal& j, PicStore& s, int brightness, int contrast, int saturation,
           const std::string& merge_key = "light");                         // "Adjust Light"
bool reset(PicJournal& j, PicStore& s);                                     // "Revert to Original"
}  // namespace pic

}  // namespace lookit::core
