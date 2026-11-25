#include "core/Pages.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace lookit::core {

std::optional<PageRec> Pages::get(const Id& id) const {
    auto it = m_items.find(id);
    if (it == m_items.end()) return std::nullopt;
    return it->second;
}

void Pages::put(const Id& id, const std::optional<PageRec>& v) {
    if (!m_loading && m_door && !m_door()) ++m_strays;   // a write outside the door
    if (v) m_items[id] = *v;
    else m_items.erase(id);
    if (m_changed && !m_loading) m_changed();
}

std::vector<Pages::Id> Pages::ids() const {
    std::vector<Id> out;
    out.reserve(m_items.size());
    for (const auto& [id, r] : m_items) out.push_back(id);
    std::stable_sort(out.begin(), out.end(),
                     [this](Id a, Id b) { return m_items.at(a).pos < m_items.at(b).pos; });
    return out;
}

std::vector<PageRec> Pages::records() const {
    std::vector<PageRec> out;
    for (Id id : ids()) out.push_back(m_items.at(id));
    return out;
}

Pages::Id Pages::id_at(int position) const {
    const auto v = ids();
    return position >= 0 && position < int(v.size()) ? v[std::size_t(position)] : -1;
}

int Pages::position_of(Id id) const {
    const auto v = ids();
    auto it = std::find(v.begin(), v.end(), id);
    return it == v.end() ? -1 : int(it - v.begin());
}

int Pages::add_source(const std::string& path) {
    m_sources.push_back(path);
    return int(m_sources.size()) - 1;
}

void Pages::load(const std::string& path, int n) {
    clear();
    ++m_loading;
    const int src = add_source(path);
    for (int i = 0; i < n; ++i) put(fresh_id(), PageRec{src, i, 0, double(i)});
    --m_loading;
}

void Pages::clear() {
    m_items.clear();
    m_sources.clear();
}

namespace pages {

namespace {
// Positions renumbered 0..n-1 for `order` -- the only way pos ever changes, so
// a move or an insert is a set of plain value changes the journal can hold.
void renumber(Pages& p, const std::vector<Pages::Id>& order) {
    for (std::size_t i = 0; i < order.size(); ++i) {
        auto r = *p.get(order[i]);
        if (r.pos != double(i)) { r.pos = double(i); p.put(order[i], r); }
    }
}
}  // namespace

bool rotate(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids, int q) {
    q = ((q % 4) + 4) % 4;
    if (ids.empty() || q == 0) return false;
    const std::string label = std::string(q == 1 ? "Rotate right" : q == 3 ? "Rotate left" : "Turn over") +
                              (ids.size() > 1 ? " (" + std::to_string(ids.size()) + " pages)" : "");
    return j.run(label, ids, [&] {
        for (auto id : ids)
            if (auto r = p.get(id)) { r->turn = (r->turn + q) % 4; p.put(id, r); }
    });
}

bool remove(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids) {
    int present = 0;
    for (auto id : ids) present += p.get(id) ? 1 : 0;
    if (present == 0 || present >= p.count()) return false;   // never empty the document
    const std::string label = present == 1 ? "Delete page" : "Delete " + std::to_string(present) + " pages";
    return j.run(label, p.ids(), [&] {
        for (auto id : ids) if (p.get(id)) p.put(id, std::nullopt);
        renumber(p, p.ids());
    });
}

bool move(PageJournal& j, Pages& p, const std::vector<Pages::Id>& ids, int to) {
    auto order = p.ids();
    std::vector<Pages::Id> moving, rest;
    for (auto id : order)
        (std::find(ids.begin(), ids.end(), id) != ids.end() ? moving : rest).push_back(id);
    if (moving.empty()) return false;
    // `to` counts positions in the FULL list; find where that lands among the rest.
    int before = 0;
    for (int i = 0; i < std::clamp(to, 0, int(order.size())); ++i)
        if (std::find(moving.begin(), moving.end(), order[std::size_t(i)]) == moving.end()) ++before;
    std::vector<Pages::Id> next(rest.begin(), rest.begin() + before);
    next.insert(next.end(), moving.begin(), moving.end());
    next.insert(next.end(), rest.begin() + before, rest.end());
    if (next == order) return false;
    const std::string label = moving.size() == 1 ? "Move page" : "Move " + std::to_string(moving.size()) + " pages";
    return j.run(label, order, [&] { renumber(p, next); });
}

std::vector<Pages::Id> insert(PageJournal& j, Pages& p, int src, int n, int position, const std::string& what) {
    std::vector<Pages::Id> made;
    if (n <= 0) return made;
    auto order = p.ids();
    position = std::clamp(position, 0, int(order.size()));
    j.run(what.empty() ? "Insert pages" : "Insert " + what, order, [&] {
        for (int i = 0; i < n; ++i) {
            const auto id = p.fresh_id();
            p.put(id, PageRec{src, i, 0, -1});
            made.push_back(id);
        }
        std::vector<Pages::Id> next(order.begin(), order.begin() + position);
        next.insert(next.end(), made.begin(), made.end());
        next.insert(next.end(), order.begin() + position, order.end());
        renumber(p, next);
        return made;
    });
    return made;
}

bool duplicate(PageJournal& j, Pages& p, Pages::Id id) {
    auto r = p.get(id);
    if (!r) return false;
    auto order = p.ids();
    const int at = p.position_of(id) + 1;
    return j.run("Duplicate page", order, [&] {
        const auto copy = p.fresh_id();
        auto c = *r;
        c.pos = -1;
        p.put(copy, c);
        std::vector<Pages::Id> next(order.begin(), order.begin() + at);
        next.push_back(copy);
        next.insert(next.end(), order.begin() + at, order.end());
        renumber(p, next);
        return std::vector<Pages::Id>{copy};
    });
}

std::vector<int> parse_ranges(const std::string& text, int count, std::string& error) {
    error.clear();
    std::vector<int> out;
    std::set<int> seen;
    std::string tok;
    auto flush = [&]() -> bool {
        std::string t;
        for (char c : tok) if (!std::isspace((unsigned char)c)) t += c;
        tok.clear();
        if (t.empty()) return true;
        int a = 0, b = 0;
        const auto dash = t.find('-');
        try {
            if (dash == std::string::npos) a = b = std::stoi(t);
            else {
                a = dash == 0 ? 1 : std::stoi(t.substr(0, dash));
                b = dash + 1 >= t.size() ? count : std::stoi(t.substr(dash + 1));
            }
        } catch (...) {
            error = "Can't read \"" + t + "\"";
            return false;
        }
        if (a < 1 || b < 1 || a > count || b > count) {
            error = "\"" + t + "\" is outside 1-" + std::to_string(count);
            return false;
        }
        const int step = a <= b ? 1 : -1;
        for (int i = a;; i += step) {
            if (seen.insert(i - 1).second) out.push_back(i - 1);
            if (i == b) break;
        }
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ';') { if (!flush()) return {}; }
        else tok += c;
    }
    if (!flush()) return {};
    return out;
}

}  // namespace pages
}  // namespace lookit::core
