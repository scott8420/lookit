#include "core/Recents.hpp"

#include "json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace lookit::core {

namespace {
std::string normalise(const std::string& path) {
    std::string s = path;
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    return s;
}
}  // namespace

void recents_add(std::vector<std::string>& list, const std::string& path,
                 std::size_t max) {
    const std::string key = normalise(path);
    if (key.empty()) return;
    auto it = std::find(list.begin(), list.end(), key);
    if (it != list.end()) list.erase(it);         // move-to-front
    list.insert(list.begin(), key);
    if (max >= 1 && list.size() > max) list.resize(max);
}

void recents_remove(std::vector<std::string>& list, const std::string& path) {
    const std::string key = normalise(path);
    auto it = std::find(list.begin(), list.end(), key);
    if (it != list.end()) list.erase(it);
}

std::vector<std::string> load_recents(const std::string& file) {
    std::vector<std::string> out;
    std::ifstream f(file);
    if (!f) return out;                            // first run / unreadable -> empty
    try {
        nlohmann::json j;
        f >> j;
        if (j.contains("paths") && j["paths"].is_array()) {
            for (const auto& e : j["paths"]) {
                if (!e.is_string()) continue;
                std::string p = normalise(e.get<std::string>());
                if (p.empty()) continue;
                std::error_code ec;
                if (!fs::exists(p, ec)) continue;  // prune stale (single point)
                if (std::find(out.begin(), out.end(), p) != out.end()) continue;  // dedupe
                out.push_back(std::move(p));
            }
        }
    } catch (const std::exception&) {
        out.clear();                               // unparseable -> empty, never throw across the seam
    }
    return out;
}

bool save_recents(const std::string& file, const std::vector<std::string>& list) {
    try {
        fs::create_directories(fs::path(file).parent_path());
    } catch (const std::exception&) {
        return false;
    }
    nlohmann::json j;
    j["paths"] = list;
    std::ofstream f(file);
    if (!f) return false;
    f << j.dump(2) << "\n";
    return static_cast<bool>(f);
}

std::string recent_menu_label(const std::string& path, std::size_t max) {
    std::string name = fs::path(path).filename().string();
    if (name.empty()) name = path;
    // Split into UTF-8 code points (a stray byte counts as one).
    std::vector<std::string> cps;
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char b = static_cast<unsigned char>(name[i]);
        std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 1;
        if (i + n > name.size()) n = 1;
        cps.push_back(name.substr(i, n));
        i += n;
    }
    if (max < 5) max = 5;
    if (cps.size() > max) {
        const std::size_t keep = max - 1;               // one for the ellipsis
        const std::size_t tail = keep * 2 / 5;          // the end (and extension)
        const std::size_t head = keep - tail;
        std::vector<std::string> cut(cps.begin(), cps.begin() + head);
        cut.push_back("\xE2\x80\xA6");                  // U+2026
        cut.insert(cut.end(), cps.end() - tail, cps.end());
        cps.swap(cut);
    }
    std::string out;
    for (const auto& cp : cps) out += (cp == "_") ? "__" : cp;
    return out;
}

}  // namespace lookit::core
