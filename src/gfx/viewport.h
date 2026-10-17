// Where the 320x240 game screen goes on a real display, and which display mode to pick.
#pragma once

#include <stdint.h>

namespace gfx {

// Upper bound on the integer scale factor: every scale step costs framebuffer bandwidth, and the framebuffer may be
// uncached on real hardware.
inline constexpr int kMaxScale = 3;

struct Viewport {
  int scale = 1;
  int x = 0;  // top-left corner on the framebuffer
  int y = 0;
  int width = 0;  // scaled size
  int height = 0;
};

// Largest integer scale (at most kMaxScale, at least 1) that fits, centered.
Viewport ComputeViewport(uint32_t fb_width, uint32_t fb_height);

struct VideoMode {
  uint32_t width = 0;
  uint32_t height = 0;
  bool usable = false;  // pixel format we can draw to
};

// Picks the mode to use: highest integer scale first, then an exact fit (no borders), then the current mode (on real
// hardware that is usually the panel's native mode, which scales without blur). Returns -1 if nothing is usable.
int ChooseVideoMode(const VideoMode* modes, int count, int current);

}  // namespace gfx
