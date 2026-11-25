#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// core/Zip -- reading a ZIP archive's table of contents (s012). GTK-free.
//
// CBZ comics and EPUB books are ZIP files. This reads the central directory
// (names, sizes, where each entry's bytes are) and hands back an entry's
// bytes as stored: method 0 (stored) is the file itself, method 8 (deflate)
// is raw deflate the caller inflates -- the UI side uses GIO's zlib
// decompressor, so no new dependency. ZIP64, encryption and spanning are
// refused ("Lookit can't read this archive").
// ─────────────────────────────────────────────────────────────────────────────
namespace lookit::core::zip {

struct Entry {
    std::string   name;            // as stored ("OEBPS/ch01.xhtml")
    std::uint16_t method = 0;      // 0 stored, 8 deflate
    std::uint32_t packed = 0;      // compressed size
    std::uint32_t size = 0;        // uncompressed size
    std::uint32_t offset = 0;      // the local header
    bool          encrypted = false;
    bool          dir() const { return !name.empty() && name.back() == '/'; }
};

// The entries of `data` (the whole file), in the order the archive lists
// them. Empty + `error` when it isn't a ZIP Lookit can read.
std::vector<Entry> list(std::string_view data, std::string& error);

// The entry's bytes as stored (still deflated for method 8). nullopt when the
// local header doesn't fit the file.
std::optional<std::string> raw(std::string_view data, const Entry& e);

// Find an entry by exact name, or -1.
int find(const std::vector<Entry>& entries, const std::string& name);

// A tiny writer for tests and fixtures: stored entries only (CRC included).
std::string make_stored(const std::vector<std::pair<std::string, std::string>>& files);

}  // namespace lookit::core::zip
