#include "core/Zip.hpp"

namespace lookit::core::zip {

namespace {
std::uint32_t u16(std::string_view d, std::size_t at) {
    return std::uint32_t(std::uint8_t(d[at])) | std::uint32_t(std::uint8_t(d[at + 1])) << 8;
}
std::uint32_t u32(std::string_view d, std::size_t at) { return u16(d, at) | u16(d, at + 2) << 16; }

void put16(std::string& s, std::uint32_t v) { s += char(v & 0xff); s += char((v >> 8) & 0xff); }
void put32(std::string& s, std::uint32_t v) { put16(s, v & 0xffff); put16(s, v >> 16); }

std::uint32_t crc32(const std::string& s) {
    std::uint32_t c = 0xffffffffu;
    for (unsigned char ch : s) {
        c ^= ch;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}
}  // namespace

std::vector<Entry> list(std::string_view d, std::string& error) {
    std::vector<Entry> out;
    // The end-of-central-directory record: in the last 64 KiB + 22 bytes.
    if (d.size() < 22) { error = "too short to be an archive"; return out; }
    std::size_t eocd = std::string::npos;
    const std::size_t lo = d.size() > 65557 ? d.size() - 65557 : 0;
    for (std::size_t i = d.size() - 22 + 1; i-- > lo;)
        if (u32(d, i) == 0x06054b50u) { eocd = i; break; }
    if (eocd == std::string::npos) { error = "not a ZIP archive"; return out; }
    const std::uint32_t count = u16(d, eocd + 10);
    const std::uint32_t cd_size = u32(d, eocd + 12), cd_at = u32(d, eocd + 16);
    if (count == 0xffff || cd_at == 0xffffffffu) { error = "a ZIP64 archive (not read yet)"; return out; }
    if (std::size_t(cd_at) + cd_size > d.size()) { error = "the archive is cut short"; return out; }
    std::size_t at = cd_at;
    for (std::uint32_t n = 0; n < count; ++n) {
        if (at + 46 > d.size() || u32(d, at) != 0x02014b50u) { error = "the archive's directory is damaged"; return {}; }
        Entry e;
        e.encrypted = u16(d, at + 8) & 1u;
        e.method = std::uint16_t(u16(d, at + 10));
        e.packed = u32(d, at + 20);
        e.size = u32(d, at + 24);
        const std::uint32_t nl = u16(d, at + 28), xl = u16(d, at + 30), cl = u16(d, at + 32);
        e.offset = u32(d, at + 42);
        if (at + 46 + nl > d.size()) { error = "the archive's directory is damaged"; return {}; }
        e.name = std::string(d.substr(at + 46, nl));
        out.push_back(std::move(e));
        at += 46 + nl + xl + cl;
    }
    return out;
}

std::optional<std::string> raw(std::string_view d, const Entry& e) {
    const std::size_t at = e.offset;
    if (at + 30 > d.size() || u32(d, at) != 0x04034b50u) return std::nullopt;
    const std::size_t start = at + 30 + u16(d, at + 26) + u16(d, at + 28);
    if (start + e.packed > d.size()) return std::nullopt;
    return std::string(d.substr(start, e.packed));
}

int find(const std::vector<Entry>& entries, const std::string& name) {
    for (int i = 0; i < int(entries.size()); ++i)
        if (entries[std::size_t(i)].name == name) return i;
    return -1;
}

std::string make_stored(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string out, cd;
    for (const auto& [name, body] : files) {
        const std::uint32_t off = std::uint32_t(out.size()), crc = crc32(body), n = std::uint32_t(body.size());
        put32(out, 0x04034b50u); put16(out, 20); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
        put32(out, crc); put32(out, n); put32(out, n); put16(out, std::uint32_t(name.size())); put16(out, 0);
        out += name;
        out += body;
        put32(cd, 0x02014b50u); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put32(cd, crc); put32(cd, n); put32(cd, n); put16(cd, std::uint32_t(name.size()));
        put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, off);
        cd += name;
    }
    const std::uint32_t cd_at = std::uint32_t(out.size());
    out += cd;
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0);
    put16(out, std::uint32_t(files.size())); put16(out, std::uint32_t(files.size()));
    put32(out, std::uint32_t(cd.size())); put32(out, cd_at); put16(out, 0);
    return out;
}

}  // namespace lookit::core::zip
