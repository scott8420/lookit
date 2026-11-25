#pragma once
#include "core/Picture.hpp"

#include <cairo.h>

// PictureArt -- the picture tools' pixels (s010): a picture as it came from the
// file, made into what you see by its edit record (core/Picture). Cairo does
// the orient / crop / resize in one paint; core::picture::light_argb32 does
// the light. The view, Export and Save all use this, so what's saved is what
// was seen. UI side (Cairo), headless-safe (lookit_savecheck runs it).
namespace lookit::picture_art {

// Steps 1-3 (orient, crop, resize) of `e` on `src` (ARGB32): a new surface,
// the caller's to destroy. Pure orient + crop is an exact pixel copy.
cairo_surface_t* shape(cairo_surface_t* src, const core::PicEdit& e);
// Step 4 on a copy of `shaped` (steps 1-3 already done): a new surface.
cairo_surface_t* light(cairo_surface_t* shaped, const core::PicEdit& e);
// All four.
cairo_surface_t* apply(cairo_surface_t* src, const core::PicEdit& e);

}  // namespace lookit::picture_art
