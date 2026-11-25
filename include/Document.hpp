#pragma once
#include "core/FileKind.hpp"
#include "core/Layout.hpp"
#include "core/Search.hpp"
#include "core/Pages.hpp"
#include "core/Forms.hpp"

#include <gdkmm/texture.h>
#include <vector>

#include <cairo.h>
#include <map>
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
    // A PDF locked with a password Lookit doesn't have (or the wrong one) gives
    // exactly kLocked: ask for it, remember_password, open again.
    static std::unique_ptr<Document> open(const std::string& path, std::string& error);
    static constexpr const char* kLocked = "This PDF is locked with a password.";

    // Passwords, per file, for this run only (never written anywhere): what
    // opened it -- poppler for the view, qpdf for Save. "" = none known.
    static void remember_password(const std::string& path, const std::string& password);
    static std::string password(const std::string& path);
    // An edited page list (the page tools) as a PDF document to view: pages
    // of `sources`, in order, turned. `path` is where it will save.
    // Drop the shared copy of `path` (it was just overwritten by a save).
    static void forget(const std::string& path);
    // `fields`: the form values the person set (PageRec::fields of Pages::kDoc,
    // keyed core::forms::key) -- shown on the pages; a field not in it shows
    // the file's own value.
    static std::shared_ptr<Document> compose(const std::vector<std::string>& sources,
                                             const std::vector<core::PageRec>& recs,
                                             const std::string& path, std::string& error,
                                             const std::map<std::string, std::string>& fields = {});
    // Save's sources (s011): each source with a value in `fields` swapped for
    // a copy poppler writes with the values filled in (and its appearance
    // drawn) -- what qpdf then copies pages from. The copies are temp files,
    // listed in `temps` for the caller to delete after the save.
    static std::vector<std::string> filled_sources(const std::vector<std::string>& sources,
                                                   const std::map<std::string, std::string>& fields,
                                                   std::vector<std::string>& temps, std::string& error);

    // A picture already in memory (ARGB32; the document takes the reference) as
    // a document at `path` -- the picture tools' edited picture (s010).
    static std::shared_ptr<Document> picture(cairo_surface_t* surface, const std::string& path);

    virtual int count() const = 0;
    virtual core::layout::Size size(int page) const = 0;   // PDF points / image pixels
    // Draw page `page` at the origin of `cr`, `scale` view pixels per unit.
    // `marks`: with its signatures and markup (thumbnails, export) or without
    // (the page view draws them itself, so a drag never re-renders paper).
    virtual void render(cairo_t* cr, int page, double scale, bool marks = true) const = 0;

    // The marks on `page` (core/Marks: the page's UNTURNED frame), the page's
    // turn, and its unturned size -- what the view needs to draw and drag them.
    virtual std::vector<core::Mark> marks(int /*page*/) const { return {}; }
    virtual int turn(int /*page*/) const { return 0; }
    virtual core::layout::Size base(int page) const { return size(page); }
    // The text the reader would select dragging from `a` to `b` (unturned
    // frame), one rectangle per line, unturned. Pictures have none.
    virtual std::vector<core::layout::Rect> text_rects(int /*page*/, core::marks::Point /*a*/,
                                                       core::marks::Point /*b*/) const { return {}; }
    // The page's text, in reading order as the file gives it ("" for pictures).
    virtual std::string text(int /*page*/) const { return {}; }
    virtual bool opaque() const { return true; }
    // A picture's pixels (ARGB32), as decoded; null for paper.
    virtual cairo_surface_t* surface() const { return nullptr; }   // paper: draw white under it
    // Every place `text` occurs on `page` (case-blind), in page units, top-left
    // origin. Kinds with no text layer find nothing.
    virtual std::vector<core::search::Hit> find(int /*page*/, const std::string& /*text*/) const { return {}; }
    // The form fields on `page` (s011): unturned frame, values as shown now.
    virtual std::vector<core::Field> fields(int /*page*/) const { return {}; }
    // Every field of every page, with its page -- Tab order, radio groups.
    std::vector<std::pair<int, core::Field>> all_fields() const;

    core::Kind kind() const { return m_kind; }
    const std::string& path() const { return m_path; }
    std::string name() const;              // the file name
    const std::string& title() const { return m_title; }   // from the file, if any

protected:
    core::Kind  m_kind = core::Kind::Unknown;
    std::string m_path;
    std::string m_title;
};

// Page `page` drawn at `scale` into a GPU texture (premultiplied BGRA, Cairo's
// ARGB32). Capped near `max_pixels`: past that the scale drops to fit. Used by
// the page view and the thumbnail strip alike.
Glib::RefPtr<Gdk::Texture> render_texture(const Document& doc, int page, double scale,
                                          double max_pixels = 16.0e6, bool marks = true);

}  // namespace lookit
