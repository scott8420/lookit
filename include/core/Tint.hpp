#pragma once
#include <optional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Tint -- the base colour of the Rouster skin, and how the skin follows it.
// (s014.)
//
// The skin (Css.hpp) is designed ONCE, in teal, as literal colours. A tint does
// not repaint it by hand: every colour literal in the stylesheet is turned in
// OKLCH -- its hue moved by the distance from Lookit teal to the chosen base,
// its chroma scaled by how colourful the base is, its LIGHTNESS LEFT ALONE.
// Lightness is what makes text readable on the gradient, so a tint can change
// the mood of the window but never its contrast. Greys and white / black have
// no hue and pass through untouched; the error red is kept out by name.
//
// The default IS the design: tint_css(css, kDefaultTint) returns css unchanged,
// byte for byte (the selftest holds it to that).
//
// GTK-free: plain strings in and out. The settings file is a tiny JSON pump in
// the same shape as Recents (the UI resolves the XDG path and hands it in).
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core {

struct Rgb { double r = 0, g = 0, b = 0; };   // sRGB, 0..1

// The logo's glass -- the colour the skin was designed in.
inline constexpr const char* kDefaultTint = "#15aabf";

struct TintPreset { std::string id, label, hex; };
// Teal (default) first; the rest hand-picked to sit well on the dark skin.
const std::vector<TintPreset>& tint_presets();

std::optional<Rgb> parse_hex(const std::string& s);   // "#rrggbb" / "rrggbb" / "#rgb"
std::string        to_hex(const Rgb& c);               // "#rrggbb", lower case
std::string        normalise_hex(const std::string& s); // "" when not a colour

// One colour of the design, turned toward `base`.
Rgb tint_color(const Rgb& literal, const Rgb& base);

// The whole stylesheet, every #rrggbb and rgba(r,g,b,a) literal turned.
// A base that isn't a colour, or is the default, returns css unchanged.
std::string tint_css(const std::string& css, const std::string& base_hex);

// The label for a hex: a preset's name, else "Custom".
std::string tint_label(const std::string& hex);

// ~/.config/lookit/settings.json : { "tint": "#rrggbb" }. load: missing,
// unreadable or not a colour -> kDefaultTint. save keeps other keys.
std::string load_tint(const std::string& file);
bool        save_tint(const std::string& file, const std::string& hex);

}  // namespace lookit::core
