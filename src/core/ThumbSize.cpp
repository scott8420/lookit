#include "core/ThumbSize.hpp"

#include "json.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace lookit::core {

int thumb_width(ThumbSize s, bool book) {
    switch (s) {
        case ThumbSize::Small: return book ? 56 : 80;
        case ThumbSize::Large: return book ? 108 : 168;
        case ThumbSize::Medium: break;
    }
    return book ? 76 : 112;   // Medium: 112 is the s004 width
}

int thumb_strip_width(ThumbSize s, bool book) {
    // 44 = the list's side padding + the row's padding + room for the scrollbar.
    return (book ? 2 * thumb_width(s, true) + kThumbSpine : thumb_width(s, false)) + 44;
}

const char* thumb_size_id(ThumbSize s) {
    switch (s) {
        case ThumbSize::Small: return "small";
        case ThumbSize::Large: return "large";
        case ThumbSize::Medium: break;
    }
    return "medium";
}

bool parse_thumb_size(const std::string& id, ThumbSize& out) {
    for (auto s : {ThumbSize::Small, ThumbSize::Medium, ThumbSize::Large})
        if (id == thumb_size_id(s)) { out = s; return true; }
    return false;
}

ThumbSize load_thumb_size(const std::string& file) {
    std::ifstream f(file);
    if (!f) return kDefaultThumbSize;
    try {
        nlohmann::json j;
        f >> j;
        ThumbSize s;
        if (j.is_object() && j.contains("thumb_size") && j["thumb_size"].is_string() &&
            parse_thumb_size(j["thumb_size"].get<std::string>(), s))
            return s;
    } catch (const std::exception&) {}
    return kDefaultThumbSize;
}

bool save_thumb_size(const std::string& file, ThumbSize s) {
    nlohmann::json j = nlohmann::json::object();
    {   // keep the base colour and whatever else is there
        std::ifstream in(file);
        if (in) {
            try { in >> j; } catch (const std::exception&) { j = nlohmann::json::object(); }
            if (!j.is_object()) j = nlohmann::json::object();
        }
    }
    j["thumb_size"] = thumb_size_id(s);
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
