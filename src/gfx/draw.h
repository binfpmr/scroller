// Basic drawing on a Surface, clipped to its bounds.
#pragma once

#include "gfx/surface.h"
#include "lib/mem.h"

namespace gfx {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;  // 0 or less: empty
  int height = 0;
};

// Intersection of the rectangle with the surface.
inline Rect ClipRect(const Surface& s, int x, int y, int width, int height) {
  const int x0 = lib::Max(x, 0);
  const int y0 = lib::Max(y, 0);
  const int x1 = lib::Min(x + width, s.width);
  const int y1 = lib::Min(y + height, s.height);
  return {x0, y0, x1 - x0, y1 - y0};
}

void FillRect(Surface& s, int x, int y, int width, int height, Color color);

}  // namespace gfx
