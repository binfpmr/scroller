#include "gfx/draw.h"

namespace gfx {

void FillRect(Surface& s, int x, int y, int width, int height, Color color) {
  const Rect r = ClipRect(s, x, y, width, height);
  for (int yy = r.y; yy < r.y + r.height; ++yy) {
    Color* row = s.Row(yy);
    for (int xx = r.x; xx < r.x + r.width; ++xx) {
      row[xx] = color;
    }
  }
}

}  // namespace gfx
