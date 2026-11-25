#include "core/Tint.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;

namespace lookit::core {

const std::vector<TintPreset>& tint_presets() {
    static const std::vector<TintPreset> p = {
        {"teal",     "Teal",     kDefaultTint},
        {"blue",     "Blue",     "#228be6"},
        {"violet",   "Violet",   "#7950f2"},
        {"rose",     "Rose",     "#e64980"},
        {"ember",    "Ember",    "#f76707"},
        {"green",    "Green",    "#2f9e44"},
        {"graphite", "Graphite", "#6c7a86"},
    };
    return p;
}

namespace {

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// ── OKLab (Bjorn Ottosson) ──────────────────────────────────────────────────
double to_lin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double to_srgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }

struct Lab { double L, a, b; };

Lab oklab(const Rgb& c) {
    const double r = to_lin(c.r), g = to_lin(c.g), b = to_lin(c.b);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

// Linear-light result; may fall outside 0..1 (out of gamut).
Rgb from_oklab_lin(const Lab& o) {
    const double l = std::pow(o.L + 0.3963377774 * o.a + 0.2158037573 * o.b, 3);
    const double m = std::pow(o.L - 0.1055613458 * o.a - 0.0638541728 * o.b, 3);
    const double s = std::pow(o.L - 0.0894841775 * o.a - 1.2914855480 * o.b, 3);
    return {+4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}

bool in_gamut(const Rgb& c) {
    const double e = 1e-4;
    return c.r >= -e && c.r <= 1 + e && c.g >= -e && c.g <= 1 + e && c.b >= -e && c.b <= 1 + e;
}

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

Rgb from_lch(double L, double C, double h) {
    // Keep L and h; give up chroma until the colour exists in sRGB.
    auto at = [&](double c) { return from_oklab_lin({L, c * std::cos(h), c * std::sin(h)}); };
    Rgb lin = at(C);
    if (!in_gamut(lin)) {
        double lo = 0, hi = C;
        for (int i = 0; i < 24; ++i) {
            const double mid = (lo + hi) / 2;
            if (in_gamut(at(mid))) lo = mid; else hi = mid;
        }
        lin = at(lo);
    }
    return {clamp01(to_srgb(clamp01(lin.r))), clamp01(to_srgb(clamp01(lin.g))), clamp01(to_srgb(clamp01(lin.b)))};
}

int byte(double v) { return static_cast<int>(std::lround(clamp01(v) * 255.0)); }

}  // namespace

std::optional<Rgb> parse_hex(const std::string& in) {
    std::string s = in;
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    if (!s.empty() && s[0] == '#') s.erase(s.begin());
    if (s.size() == 3) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6) return std::nullopt;
    int v[6];
    for (int i = 0; i < 6; ++i) if ((v[i] = hexval(s[i])) < 0) return std::nullopt;
    return Rgb{(v[0] * 16 + v[1]) / 255.0, (v[2] * 16 + v[3]) / 255.0, (v[4] * 16 + v[5]) / 255.0};
}

std::string to_hex(const Rgb& c) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", byte(c.r), byte(c.g), byte(c.b));
    return buf;
}

std::string normalise_hex(const std::string& s) {
    auto c = parse_hex(s);
    return c ? to_hex(*c) : std::string();
}

Rgb tint_color(const Rgb& literal, const Rgb& base) {
    const Lab ref = oklab(*parse_hex(kDefaultTint));
    const Lab to  = oklab(base);
    const Lab lit = oklab(literal);
    const double ref_c = std::hypot(ref.a, ref.b);
    const double to_c  = std::hypot(to.a, to.b);
    const double lit_c = std::hypot(lit.a, lit.b);
    if (lit_c < 0.004) return literal;                 // a grey: no hue to move
    // Hue moves by the same angle for every literal (the design's relations
    // hold); chroma scales with how colourful the base is, capped so a vivid
    // pick can't outshout the pages. Lightness stays -- that is the contrast.
    const double dh = std::atan2(to.b, to.a) - std::atan2(ref.b, ref.a);
    const double k  = std::clamp(to_c / ref_c, 0.0, 1.15);
    const double h  = std::atan2(lit.b, lit.a) + dh;
    return from_lch(lit.L, lit_c * k, h);
}

std::string tint_css(const std::string& css, const std::string& base_hex) {
    const std::string hex = normalise_hex(base_hex);
    if (hex.empty() || hex == kDefaultTint) return css;
    const Rgb base = *parse_hex(hex);
    // Kept as designed, whatever the tint: the error toast's red.
    static const char* keep[] = {"#ff8787", "#ffe3e3"};

    static const std::regex lit(R"(#([0-9a-fA-F]{6})\b|rgba\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([0-9.]+)\s*\))");
    std::string out;
    out.reserve(css.size());
    auto begin = std::sregex_iterator(css.begin(), css.end(), lit);
    std::size_t last = 0;
    for (auto it = begin; it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        out.append(css, last, static_cast<std::size_t>(m.position()) - last);
        last = static_cast<std::size_t>(m.position() + m.length());
        if (m[1].matched) {
            const std::string h = normalise_hex(m.str(0));
            if (std::find(std::begin(keep), std::end(keep), h) != std::end(keep)) { out += m.str(0); continue; }
            out += to_hex(tint_color(*parse_hex(h), base));
        } else {
            const Rgb c{std::stoi(m.str(2)) / 255.0, std::stoi(m.str(3)) / 255.0, std::stoi(m.str(4)) / 255.0};
            const Rgb t = tint_color(c, base);
            out += "rgba(" + std::to_string(byte(t.r)) + "," + std::to_string(byte(t.g)) + "," +
                   std::to_string(byte(t.b)) + "," + m.str(5) + ")";
        }
    }
    out.append(css, last, std::string::npos);
    return out;
}

std::string tint_label(const std::string& hex) {
    const std::string h = normalise_hex(hex);
    for (const auto& p : tint_presets())
        if (p.hex == h) return p.label;
    return "Custom";
}

std::string load_tint(const std::string& file) {
    std::ifstream f(file);
    if (!f) return kDefaultTint;
    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("tint") && j["tint"].is_string()) {
            const std::string h = normalise_hex(j["tint"].get<std::string>());
            if (!h.empty()) return h;
        }
    } catch (const std::exception&) {}
    return kDefaultTint;
}

bool save_tint(const std::string& file, const std::string& hex) {
    const std::string h = normalise_hex(hex);
    if (h.empty()) return false;
    nlohmann::json j = nlohmann::json::object();
    {   // keep whatever else a later setting put there
        std::ifstream in(file);
        if (in) {
            try { in >> j; } catch (const std::exception&) { j = nlohmann::json::object(); }
            if (!j.is_object()) j = nlohmann::json::object();
        }
    }
    j["tint"] = h;
    std::error_code ec;
    fs::create_directories(fs::path(file).parent_path(), ec);
    const std::string tmp = file + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return false;
        out << j.dump(2) << "\n";
        if (!out) return false;
    }
    fs::rename(tmp, file, ec);
    return !ec;
}

}  // namespace lookit::core
