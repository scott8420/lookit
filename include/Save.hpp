#pragma once
#include "core/Pages.hpp"

#include <string>
#include <vector>

// Save -- writing PDFs. The edited page list (core::Pages) becomes a real PDF
// with qpdf: each record's page copied from its source, turned, in order.
// Written to a temp file beside the target and renamed over it, so a failed
// save never leaves a half-written PDF where the good one was.
namespace lookit::save {

// Write `recs` (pages of `sources`) to `out_path`. False + `error` on failure.
// A source locked with a password opens with Document::password(its path).
// Form fields come along with their pages (s011). `lock` says how the written
// file is locked: Keep -- as the first source is (its own password and
// permissions, or none); None -- open; Password -- AES-256 with that password.
bool write_pdf(const std::vector<std::string>& sources, const std::vector<core::PageRec>& recs,
               const std::string& out_path, std::string& error, const core::Lock& lock = {});

// A picture as a one-page PDF in the cache dir (150 dpi), for inserting and
// combining. Returns the PDF's path, or "" + `error`.
std::string picture_to_pdf(const std::string& picture, std::string& error);

}  // namespace lookit::save
