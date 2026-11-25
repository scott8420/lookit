#include "core/Book.hpp"
#include "core/Picture.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace lookit::core {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
bool ends_with(const std::string& s, const std::string& x) {
    return s.size() >= x.size() && s.compare(s.size() - x.size(), x.size(), x) == 0;
}
std::uint32_t be16(const std::string& d, std::size_t at) {
    return std::uint32_t(std::uint8_t(d[at])) << 8 | std::uint8_t(d[at + 1]);
}
std::uint32_t be32(const std::string& d, std::size_t at) { return be16(d, at) << 16 | be16(d, at + 2); }
std::uint32_t le16(const std::string& d, std::size_t at) {
    return std::uint32_t(std::uint8_t(d[at])) | std::uint32_t(std::uint8_t(d[at + 1])) << 8;
}
std::uint32_t le32(const std::string& d, std::size_t at) { return le16(d, at) | le16(d, at + 2) << 16; }

// UTF-8 for a code point.
std::string utf8(unsigned long c) {
    std::string s;
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xc0 | (c >> 6)); s += char(0x80 | (c & 0x3f)); }
    else if (c < 0x10000) { s += char(0xe0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3f)); s += char(0x80 | (c & 0x3f)); }
    else if (c < 0x110000) {
        s += char(0xf0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3f));
        s += char(0x80 | ((c >> 6) & 0x3f)); s += char(0x80 | (c & 0x3f));
    }
    return s;
}

// The value of attribute `name` inside one tag's text ("" when absent).
std::string attr(const std::string& tag, const std::string& name) {
    std::size_t at = 0;
    const std::string low = lower(tag);
    while ((at = low.find(name, at)) != std::string::npos) {
        const bool start_ok = at > 0 && (std::isspace(static_cast<unsigned char>(low[at - 1])) || low[at - 1] == ':');
        std::size_t p = at + name.size();
        while (p < low.size() && std::isspace(static_cast<unsigned char>(low[p]))) ++p;
        if (start_ok && p < low.size() && low[p] == '=') {
            ++p;
            while (p < low.size() && std::isspace(static_cast<unsigned char>(low[p]))) ++p;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                const char q = tag[p];
                const auto e = tag.find(q, p + 1);
                if (e != std::string::npos) return tag.substr(p + 1, e - p - 1);
            }
        }
        at += name.size();
    }
    return {};
}

// Every tag named `name` (any namespace prefix), as its text between < and >.
std::vector<std::string> tags(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while ((at = xml.find('<', at)) != std::string::npos) {
        const auto end = xml.find('>', at);
        if (end == std::string::npos) break;
        std::string t = xml.substr(at + 1, end - at - 1);
        std::size_t k = 0;
        while (k < t.size() && !std::isspace(static_cast<unsigned char>(t[k])) && t[k] != '/') ++k;
        std::string tn = lower(t.substr(0, k));
        if (auto c = tn.find(':'); c != std::string::npos) tn = tn.substr(c + 1);
        if (tn == name) out.push_back(t);
        at = end + 1;
    }
    return out;
}

std::string decode_entities(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        const auto semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += '&'; continue; }
        const std::string e = s.substr(i + 1, semi - i - 1);
        std::string r;
        if (e == "amp") r = "&";
        else if (e == "lt") r = "<";
        else if (e == "gt") r = ">";
        else if (e == "quot") r = "\"";
        else if (e == "apos") r = "'";
        else if (e == "nbsp") r = utf8(0xa0);
        else if (e == "mdash") r = utf8(0x2014);
        else if (e == "ndash") r = utf8(0x2013);
        else if (e == "hellip") r = utf8(0x2026);
        else if (e == "lsquo") r = utf8(0x2018);
        else if (e == "rsquo") r = utf8(0x2019);
        else if (e == "ldquo") r = utf8(0x201c);
        else if (e == "rdquo") r = utf8(0x201d);
        else if (!e.empty() && e[0] == '#') {
            const bool hex = e.size() > 1 && (e[1] == 'x' || e[1] == 'X');
            const unsigned long c = std::strtoul(e.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            if (c) r = utf8(c);
        }
        if (r.empty()) { out += '&'; continue; }
        out += r;
        i = semi;
    }
    return out;
}

std::string fold_space(const std::string& s) {
    std::string out;
    bool sp = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // A no-break space (C2 A0) stays a space you see, not a break.
        if (std::isspace(c)) { sp = true; continue; }
        if (sp && !out.empty()) out += ' ';
        sp = false;
        out += char(c);
    }
    return out;
}
}  // namespace

// ── comic ───────────────────────────────────────────────────────────────────
namespace comic {

bool is_picture_name(const std::string& name) {
    const std::string n = lower(name);
    for (const char* x : {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".avif", ".jxl", ".tif", ".tiff"})
        if (ends_with(n, x)) return true;
    return false;
}

std::vector<int> pages(const std::vector<zip::Entry>& entries) {
    std::vector<int> out;
    for (int i = 0; i < int(entries.size()); ++i) {
        const auto& e = entries[std::size_t(i)];
        if (e.dir() || e.encrypted || !is_picture_name(e.name)) continue;
        if (e.name.rfind("__MACOSX/", 0) == 0) continue;
        const auto slash = e.name.find_last_of('/');
        const std::string base = slash == std::string::npos ? e.name : e.name.substr(slash + 1);
        if (base.empty() || base[0] == '.') continue;
        out.push_back(i);
    }
    std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
        return picture::natural_less(entries[std::size_t(a)].name, entries[std::size_t(b)].name);
    });
    return out;
}

std::optional<std::pair<int, int>> image_size(const std::string& d) {
    auto ok = [](std::uint32_t w, std::uint32_t h) -> std::optional<std::pair<int, int>> {
        if (w == 0 || h == 0 || w > 100000 || h > 100000) return std::nullopt;
        return std::pair<int, int>{int(w), int(h)};
    };
    if (d.size() >= 24 && d.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) return ok(be32(d, 16), be32(d, 20));
    if (d.size() >= 10 && (d.compare(0, 6, "GIF87a") == 0 || d.compare(0, 6, "GIF89a") == 0))
        return ok(le16(d, 6), le16(d, 8));
    if (d.size() >= 26 && d.compare(0, 2, "BM") == 0) {
        const std::int32_t h = std::int32_t(le32(d, 22));
        return ok(le32(d, 18), std::uint32_t(h < 0 ? -h : h));
    }
    if (d.size() >= 30 && d.compare(0, 4, "RIFF") == 0 && d.compare(8, 4, "WEBP") == 0) {
        if (d.compare(12, 4, "VP8 ") == 0) return ok(le16(d, 26) & 0x3fff, le16(d, 28) & 0x3fff);
        if (d.compare(12, 4, "VP8L") == 0 && d.size() >= 25) {
            const std::uint32_t b = le32(d, 21);
            return ok((b & 0x3fff) + 1, ((b >> 14) & 0x3fff) + 1);
        }
        if (d.compare(12, 4, "VP8X") == 0) {
            const std::uint32_t w = le16(d, 24) | std::uint32_t(std::uint8_t(d[26])) << 16;
            const std::uint32_t h = le16(d, 27) | std::uint32_t(std::uint8_t(d[29])) << 16;
            return ok(w + 1, h + 1);
        }
        return std::nullopt;
    }
    if (d.size() >= 4 && std::uint8_t(d[0]) == 0xff && std::uint8_t(d[1]) == 0xd8) {
        std::size_t at = 2;
        while (at + 9 < d.size()) {
            if (std::uint8_t(d[at]) != 0xff) { ++at; continue; }
            const std::uint8_t m = std::uint8_t(d[at + 1]);
            if (m == 0xff) { ++at; continue; }
            if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) { at += 2; continue; }
            const std::uint32_t len = be16(d, at + 2);
            // SOF0..SOF15 but not DHT (C4), JPG (C8), DAC (CC)
            if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc)
                return ok(be16(d, at + 7), be16(d, at + 5));
            if (m == 0xda) break;   // the scan: no frame header found before it
            at += 2 + len;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace comic

// ── epub ────────────────────────────────────────────────────────────────────
namespace epub {

std::string resolve(const std::string& base, const std::string& href_in) {
    std::string href = href_in.substr(0, href_in.find('#'));
    std::string dec;
    for (std::size_t i = 0; i < href.size(); ++i) {
        if (href[i] == '%' && i + 2 < href.size() && std::isxdigit(static_cast<unsigned char>(href[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(href[i + 2]))) {
            dec += char(std::strtoul(href.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            dec += href[i];
        }
    }
    std::string path = dec;
    if (!dec.empty() && dec[0] == '/') path = dec.substr(1);
    else {
        const auto slash = base.find_last_of('/');
        path = (slash == std::string::npos ? std::string{} : base.substr(0, slash + 1)) + dec;
    }
    std::vector<std::string> parts;
    std::size_t at = 0;
    while (at <= path.size()) {
        const auto s = path.find('/', at);
        const std::string p = path.substr(at, s == std::string::npos ? std::string::npos : s - at);
        if (p == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!p.empty() && p != ".") parts.push_back(p);
        if (s == std::string::npos) break;
        at = s + 1;
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}

std::string package_path(const std::string& xml) {
    for (const auto& t : tags(xml, "rootfile")) {
        const std::string mt = attr(t, "media-type");
        const std::string fp = attr(t, "full-path");
        if (!fp.empty() && (mt.empty() || mt == "application/oebps-package+xml")) return fp;
    }
    return {};
}

Package parse_package(const std::string& xml, const std::string& opf_path) {
    Package p;
    // <dc:title>...</dc:title>
    {
        const std::string low = lower(xml);
        auto at = low.find("<dc:title");
        if (at == std::string::npos) at = low.find("<title");
        if (at != std::string::npos) {
            const auto open_end = xml.find('>', at);
            const auto close = low.find("</", open_end);
            if (open_end != std::string::npos && close != std::string::npos)
                p.title = fold_space(decode_entities(xml.substr(open_end + 1, close - open_end - 1)));
        }
    }
    struct Item { std::string id, href, type, props; };
    std::vector<Item> items;
    for (const auto& t : tags(xml, "item"))
        items.push_back({attr(t, "id"), attr(t, "href"), attr(t, "media-type"), attr(t, "properties")});
    auto by_id = [&](const std::string& id) -> const Item* {
        for (const auto& it : items) if (it.id == id) return &it;
        return nullptr;
    };
    for (const auto& t : tags(xml, "itemref")) {
        if (attr(t, "linear") == "no") continue;
        if (const Item* it = by_id(attr(t, "idref")); it && !it->href.empty())
            p.spine.push_back(resolve(opf_path, it->href));
    }
    // The cover: EPUB 3's properties="cover-image", else EPUB 2's <meta name="cover">.
    for (const auto& it : items)
        if (it.props.find("cover-image") != std::string::npos) { p.cover = resolve(opf_path, it.href); break; }
    if (p.cover.empty())
        for (const auto& t : tags(xml, "meta"))
            if (attr(t, "name") == "cover")
                if (const Item* it = by_id(attr(t, "content")); it && it->type.rfind("image/", 0) == 0)
                    p.cover = resolve(opf_path, it->href);
    return p;
}

std::vector<Block> blocks(const std::string& x) {
    std::vector<Block> out;
    Block cur;
    auto flush = [&] {
        cur.text = fold_space(decode_entities(cur.text));
        if (!cur.text.empty()) out.push_back(cur);
        cur = {};
    };
    std::size_t i = 0;
    int skip = 0;   // inside head / script / style
    while (i < x.size()) {
        if (x[i] != '<') {
            if (!skip) cur.text += x[i];
            ++i;
            continue;
        }
        if (x.compare(i, 4, "<!--") == 0) {
            const auto e = x.find("-->", i);
            i = e == std::string::npos ? x.size() : e + 3;
            continue;
        }
        const auto e = x.find('>', i);
        if (e == std::string::npos) break;
        std::string t = x.substr(i + 1, e - i - 1);
        i = e + 1;
        const bool closing = !t.empty() && t[0] == '/';
        std::size_t k = closing ? 1 : 0;
        std::size_t n = k;
        while (n < t.size() && !std::isspace(static_cast<unsigned char>(t[n])) && t[n] != '/') ++n;
        std::string name = lower(t.substr(k, n - k));
        if (auto c = name.find(':'); c != std::string::npos) name = name.substr(c + 1);
        const bool self = !t.empty() && t.back() == '/';
        if (name == "head" || name == "script" || name == "style") {
            if (!self) skip += closing ? -1 : 1;
            if (skip < 0) skip = 0;
            continue;
        }
        if (skip) continue;
        const bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        if (heading) { flush(); cur.heading = !closing; continue; }
        if (name == "p" || name == "div" || name == "li" || name == "br" || name == "blockquote" || name == "tr" ||
            name == "section" || name == "dt" || name == "dd" || name == "pre" || name == "hr") {
            const bool h = cur.heading;
            flush();
            cur.heading = h && !closing;
        }
    }
    flush();
    return out;
}

}  // namespace epub

// ── words ───────────────────────────────────────────────────────────────────
namespace words {

std::vector<layout::Rect> find(const std::vector<Word>& ws, const std::string& query) {
    std::vector<layout::Rect> out;
    const std::string q = lower(fold_space(query));
    if (q.empty() || ws.empty()) return out;
    std::string s;
    std::vector<int> owner;   // the word each char of s belongs to (-1: a joining space)
    for (int i = 0; i < int(ws.size()); ++i) {
        if (i) { s += ' '; owner.push_back(-1); }
        const std::string t = lower(ws[std::size_t(i)].text);
        s += t;
        owner.insert(owner.end(), t.size(), i);
    }
    std::size_t at = 0;
    while ((at = s.find(q, at)) != std::string::npos) {
        int a = owner[at], b = owner[at + q.size() - 1];
        if (a < 0) a = owner[at + 1];
        if (b < 0) b = owner[at + q.size() - 2];
        // One box per line: a word that starts a new line (left of the last,
        // or below it) starts a new box.
        layout::Rect box = ws[std::size_t(a)].box;
        for (int k = a + 1; k <= b; ++k) {
            const auto& w = ws[std::size_t(k)].box;
            const auto& p = ws[std::size_t(k - 1)].box;
            const bool new_line = w.x < p.x || w.y > p.y + p.h * 0.6;
            if (new_line) { out.push_back(box); box = w; continue; }
            const double x0 = std::min(box.x, w.x), y0 = std::min(box.y, w.y);
            const double x1 = std::max(box.x + box.w, w.x + w.w), y1 = std::max(box.y + box.h, w.y + w.h);
            box = {x0, y0, x1 - x0, y1 - y0};
        }
        out.push_back(box);
        at += q.size();
    }
    return out;
}

}  // namespace words

}  // namespace lookit::core
