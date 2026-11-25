#pragma once
#include "Document.hpp"

#include <string>
#include <vector>

// Export -- a document's pages out as pictures, a picture as another format,
// a PDF's text as .txt. What's written is what's on screen: the page list as
// edited (turned, reordered, signed), saved or not.
namespace lookit::exporter {

enum class Format { Png, Jpeg, Tiff, Bmp, Pdf, Text };

const char* ext(Format f);      // "png", "jpg", ...
const char* label(Format f);    // "PNG", "JPEG", ...

// The formats a document of this kind can go to, in menu order.
std::vector<Format> formats_for(const Document& doc);
// The DPIs offered for pages (pictures are written at their own size).
const std::vector<int>& dpis();

// One page as a picture at `dpi` (a picture document: at its own pixels, `dpi`
// ignored). PDF pages are paper -- always opaque; a picture keeps its clear
// parts in PNG / TIFF and goes onto white in JPEG / BMP.
bool write_page(const Document& doc, int page, int dpi, Format f, const std::string& path, std::string& error);
// A picture as a one-page PDF (150 dpi, as Combine makes them).
bool write_picture_pdf(const Document& doc, const std::string& path, std::string& error);
// The text of `pages`, in order, each page ended by a form feed (as pdftotext).
bool write_text(const Document& doc, const std::vector<int>& pages, const std::string& path, std::string& error);

}  // namespace lookit::exporter
