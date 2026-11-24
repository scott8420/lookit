#pragma once
#include <string>

// core/FileKind -- what a file is, by its first bytes first and its name second
// (a .pdf that is really a PNG opens as a picture). GTK-free.
namespace lookit::core {

enum class Kind { Unknown, Pdf, Image, DjVu, Epub, Cbz };

Kind kind_from_magic(const std::string& head);   // the first 64 bytes or so
Kind kind_from_name(const std::string& path);    // by extension, case-blind
Kind detect(const std::string& path);            // reads the head, then the name
const char* kind_label(Kind k);                  // "PDF", "Picture", ...

}  // namespace lookit::core
