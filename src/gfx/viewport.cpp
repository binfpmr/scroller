#include "gfx/viewport.h"

#include "gfx/surface.h"
#include "lib/mem.h"

namespace gfx {
namespace {

int ScaleFor(uint32_t width, uint32_t height) {
  const uint32_t scale = lib::Min(width / kScreenWidth, height / kScreenHeight);
  return static_cast<int>(lib::Min<uint32_t>(scale, kMaxScale));
}

}  // namespace

Viewport ComputeViewport(uint32_t fb_width, uint32_t fb_height) {
  Viewport vp;
  vp.scale = lib::Max(ScaleFor(fb_width, fb_height), 1);
  vp.width = kScreenWidth * vp.scale;
  vp.height = kScreenHeight * vp.scale;
  vp.x = lib::Max((static_cast<int>(fb_width) - vp.width) / 2, 0);
  vp.y = lib::Max((static_cast<int>(fb_height) - vp.height) / 2, 0);
  return vp;
}

int ChooseVideoMode(const VideoMode* modes, int count, int current) {
  if (current >= 0 && current < count && modes[current].usable &&
      ScaleFor(modes[current].width, modes[current].height) >= 1) {
    return current;
  }
  int best = -1;
  int best_scale = 0;
  bool best_exact = false;
  for (int i = 0; i < count; ++i) {
    const VideoMode& m = modes[i];
    const int scale = ScaleFor(m.width, m.height);
    if (!m.usable || scale < 1) {
      continue;
    }
    const bool exact = m.width == static_cast<uint32_t>(kScreenWidth * scale) &&
                       m.height == static_cast<uint32_t>(kScreenHeight * scale);
    if (best < 0 || scale > best_scale || (scale == best_scale && exact && !best_exact)) {
      best = i;
      best_scale = scale;
      best_exact = exact;
    }
  }
  return best;
}

}  // namespace gfx
