// Where the 320x240 game screen goes on a real display, and which display mode to pick.
#pragma once

#include <stdint.h>

namespace gfx {

// Upper bound on the integer scale factor (covers 5120x3840). Only limits the row buffer in gfx::Present; with a
// write-combining framebuffer even a full 4K screen presents fast enough.
inline constexpr int kMaxScale = 16;

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

// Picks the mode to use. The current mode wins whenever it is usable and at least 320x240: on real hardware it is the
// panel's native mode, and any other mode would be rescaled (blurred) by the panel; in QEMU it is the preferred
// resolution tools/qemu.py sets. Otherwise: the highest integer scale, then an exact fit (no borders).
// Returns -1 if nothing is usable.
int ChooseVideoMode(const VideoMode* modes, int count, int current);

}  // namespace gfx
