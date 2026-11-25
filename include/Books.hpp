#pragma once
#include "Document.hpp"

#include <memory>
#include <string>

// Books -- the Document kinds of s012 (src/Books.cpp): DjVu (djvulibre),
// CBZ comics and EPUB books (ZIP via core/Zip, no new dependency). Document::
// open calls these; nothing else needs to know they exist.
namespace lookit::books {

std::unique_ptr<Document> open_djvu(const std::string& path, std::string& error);
std::unique_ptr<Document> open_cbz(const std::string& path, std::string& error);
std::unique_ptr<Document> open_epub(const std::string& path, std::string& error);

}  // namespace lookit::books
