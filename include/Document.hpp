#pragma once
#include "core/FileKind.hpp"
#include "core/Layout.hpp"

#include <cairo.h>
#include <memory>
#include <string>

// Document -- one open file, whatever its kind, as a list of pages that can be
// measured and drawn. The view only ever talks to this; a new kind (DjVu, CBZ)
// is a new subclass, nothing else. UI-side (poppler, GDK), so not in core/.
namespace lookit {

class Document {
public:
    virtual ~Document() = default;

    // Open `path`, or return null with `error` saying why, in words for a person.
    static std::unique_ptr<Document> open(const std::string& path, std::string& error);

    virtual int count() const = 0;
    virtual core::layout::Size size(int page) const = 0;   // PDF points / image pixels
    // Draw page `page` at the origin of `cr`, `scale` view pixels per unit.
    virtual void render(cairo_t* cr, int page, double scale) const = 0;
    virtual bool opaque() const { return true; }   // paper: draw white under it

    core::Kind kind() const { return m_kind; }
    const std::string& path() const { return m_path; }
    std::string name() const;              // the file name
    const std::string& title() const { return m_title; }   // from the file, if any

protected:
    core::Kind  m_kind = core::Kind::Unknown;
    std::string m_path;
    std::string m_title;
};

}  // namespace lookit
