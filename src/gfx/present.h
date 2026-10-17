// Copies the game surface to the hardware framebuffer: integer scaling plus pixel order conversion.
#pragma once

#include "gfx/framebuffer.h"
#include "gfx/surface.h"
#include "gfx/viewport.h"

namespace gfx {

// Fills the whole framebuffer (e.g. the borders around the viewport) once.
void ClearFramebuffer(const Framebuffer& fb, Color color);

// `src` must be kScreenWidth x kScreenHeight and `vp` must come from ComputeViewport for this framebuffer.
void Present(const Surface& src, const Framebuffer& fb, const Viewport& vp);

}  // namespace gfx
