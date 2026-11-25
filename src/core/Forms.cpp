#include "core/Forms.hpp"

#include <algorithm>
#include <cstdlib>

namespace lookit::core::forms {

std::string key(int src, int id) { return std::to_string(src) + ":" + std::to_string(id); }

const char* noun(FieldKind k) {
    switch (k) {
        case FieldKind::Text:      return "text field";
        case FieldKind::Check:     return "check box";
        case FieldKind::Radio:     return "choice";
        case FieldKind::Combo:     return "drop-down";
        case FieldKind::List:      return "list";
        case FieldKind::Push:      return "button";
        case FieldKind::Signature: return "signature field";
        default:                   return "field";
    }
}

bool fillable(const Field& f) {
    if (f.read_only) return false;
    switch (f.kind) {
        case FieldKind::Text: case FieldKind::Check: case FieldKind::Radio:
        case FieldKind::Combo: case FieldKind::List: return true;
        default: return false;
    }
}

std::string on(bool yes) { return yes ? "1" : "0"; }
bool is_on(const std::string& v) { return v == "1"; }

std::string picks(const std::vector<int>& items) {
    std::vector<int> v = items;
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    std::string s = "#";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
    return s;
}

std::vector<int> picked(const std::string& v) {
    std::vector<int> out;
    if (v.empty() || v[0] != '#') return out;
    std::size_t at = 1;
    while (at < v.size()) {
        const auto comma = v.find(',', at);
        const std::string tok = v.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        if (!tok.empty() && tok.find_first_not_of("0123456789") == std::string::npos)
            out.push_back(std::atoi(tok.c_str()));
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return out;
}

std::string typed(const std::string& words) { return "=" + words; }
bool is_typed(const std::string& v) { return !v.empty() && v[0] == '='; }
std::string typed_words(const std::string& v) { return is_typed(v) ? v.substr(1) : std::string{}; }

std::string shown(const Field& f, const std::string& v) {
    switch (f.kind) {
        case FieldKind::Check: case FieldKind::Radio: return is_on(v) ? "on" : "off";
        case FieldKind::Combo: case FieldKind::List: {
            if (is_typed(v)) return typed_words(v);
            std::string s;
            for (int i : picked(v))
                if (i >= 0 && i < int(f.options.size())) s += (s.empty() ? "" : ", ") + f.options[std::size_t(i)];
            return s;
        }
        default: return v;
    }
}

int hit(const std::vector<Field>& fields, double x, double y) {
    int best = -1;
    double area = 0;
    for (int i = 0; i < int(fields.size()); ++i) {
        const auto& b = fields[std::size_t(i)].box;
        if (x < b.x || y < b.y || x > b.x + b.w || y > b.y + b.h) continue;
        const double a = b.w * b.h;
        if (best < 0 || a < area) { best = i; area = a; }
    }
    return best;
}

std::vector<int> tab_order(const std::vector<std::pair<int, Field>>& fields) {
    std::vector<int> idx(fields.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = int(i);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        const auto& [pa, fa] = fields[std::size_t(a)];
        const auto& [pb, fb] = fields[std::size_t(b)];
        if (pa != pb) return pa < pb;
        // Same line when their tops are within half the shorter one's height.
        const double tol = 0.5 * std::min(fa.box.h, fb.box.h);
        if (std::abs(fa.box.y - fb.box.y) > tol) return fa.box.y < fb.box.y;
        return fa.box.x < fb.box.x;
    });
    return idx;
}

namespace {
std::string label_for(const Field& f, const std::string& value) {
    const std::string q = "“" + f.name + "”";
    switch (f.kind) {
        case FieldKind::Check: return std::string(is_on(value) ? "Tick " : "Untick ") + q;
        case FieldKind::Radio: case FieldKind::Combo: case FieldKind::List: return "Choose " + q;
        default: return "Fill in " + q;
    }
}

// The group's members, f among them (a group passed without it still has it).
std::vector<Field> members(const Field& f, const std::vector<Field>& group) {
    std::vector<Field> out;
    bool has = false;
    for (const auto& g : group) {
        if (g.src != f.src || g.name != f.name || g.kind != f.kind) continue;
        has = has || g.id == f.id;
        if (std::none_of(out.begin(), out.end(), [&](const Field& o) { return o.id == g.id; })) out.push_back(g);
    }
    if (!has) out.push_back(f);
    return out;
}
}  // namespace

bool set(PageJournal& j, Pages& p, const Field& f, const std::vector<Field>& group, const std::string& value) {
    if (!fillable(f)) return false;
    auto d = p.doc();
    const auto all = members(f, group);
    bool any = false;
    for (const auto& g : all) {
        // A radio: the chosen one on, the rest of the group off. Anything
        // else: the whole group shows the one value.
        const std::string v = f.kind == FieldKind::Radio ? on(g.id == f.id && is_on(value)) : value;
        const auto k = key(g.src, g.id);
        const auto it = d.fields.find(k);
        const std::string now = it != d.fields.end() ? it->second : g.value;
        if (now != v) any = true;
        d.fields[k] = v;
    }
    // A radio that is already the chosen one is not a step (no toggling off).
    if (!any) return false;
    return j.run(label_for(f, value), {Pages::kDoc}, [&] { p.put(Pages::kDoc, d); });
}

bool reset(PageJournal& j, Pages& p, const Field& f, const std::vector<Field>& group) {
    auto d = p.doc();
    bool any = false;
    for (const auto& g : members(f, group)) any = d.fields.erase(key(g.src, g.id)) > 0 || any;
    if (!any) return false;
    return j.run("Clear “" + f.name + "”", {Pages::kDoc}, [&] { p.put(Pages::kDoc, d); });
}

int changed(const Pages& p) { return int(p.doc().fields.size()); }

}  // namespace lookit::core::forms
