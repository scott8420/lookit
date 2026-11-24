#include "core/FileKind.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace lookit::core {

namespace {
bool starts(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

std::string ext_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}
}  // namespace

Kind kind_from_magic(const std::string& h) {
    if (starts(h, "%PDF")) return Kind::Pdf;
    if (starts(h, "AT&TFORM")) return Kind::DjVu;
    if (starts(h, "\x89PNG") || starts(h, "\xFF\xD8\xFF") || starts(h, "GIF8") ||
        starts(h, "BM") || starts(h, std::string("II*\0", 4)) || starts(h, std::string("MM\0*", 4)))
        return Kind::Image;
    if (starts(h, "RIFF") && h.size() >= 12 && h.compare(8, 4, "WEBP") == 0) return Kind::Image;
    if (h.size() >= 12 && h.compare(4, 4, "ftyp") == 0) {   // HEIF / AVIF
        const std::string brand = h.substr(8, 4);
        if (brand == "heic" || brand == "heix" || brand == "mif1" || brand == "avif") return Kind::Image;
    }
    if (starts(h, "PK\x03\x04") && h.size() >= 58 &&
        h.compare(30, 28, "mimetypeapplication/epub+zip") == 0)
        return Kind::Epub;
    return Kind::Unknown;   // a plain zip is decided by its name (CBZ)
}

Kind kind_from_name(const std::string& path) {
    const std::string e = ext_of(path);
    if (e == "pdf") return Kind::Pdf;
    if (e == "djvu" || e == "djv") return Kind::DjVu;
    if (e == "epub") return Kind::Epub;
    if (e == "cbz") return Kind::Cbz;
    for (const char* x : {"png", "jpg", "jpeg", "gif", "bmp", "tif", "tiff", "webp", "svg",
                          "heic", "heif", "avif", "ico", "tga", "jxl"})
        if (e == x) return Kind::Image;
    return Kind::Unknown;
}

Kind detect(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string head(64, '\0');
    if (f) {
        f.read(head.data(), std::streamsize(head.size()));
        head.resize(std::size_t(f.gcount()));
    } else {
        head.clear();
    }
    const Kind m = kind_from_magic(head);
    if (m != Kind::Unknown) return m;
    const Kind n = kind_from_name(path);
    if (n == Kind::Cbz && !starts(head, "PK\x03\x04")) return Kind::Unknown;
    return n;
}

const char* kind_label(Kind k) {
    switch (k) {
        case Kind::Pdf:   return "PDF";
        case Kind::Image: return "Picture";
        case Kind::DjVu:  return "DjVu";
        case Kind::Epub:  return "EPUB";
        case Kind::Cbz:   return "Comic (CBZ)";
        default:          return "Unknown";
    }
}

}  // namespace lookit::core
