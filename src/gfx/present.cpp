#include "gfx/present.h"

#include "lib/mem.h"

namespace gfx {
namespace {

uint32_t ToFramebuffer(Color c, PixelOrder order) {
  if (order == PixelOrder::kBgrx) {
    return c;
  }
  return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
}

}  // namespace

void ClearFramebuffer(const Framebuffer& fb, Color color) {
  const uint32_t pixel = ToFramebuffer(color, fb.order);
  for (uint32_t y = 0; y < fb.height; ++y) {
    uint32_t* row = fb.base + static_cast<size_t>(y) * fb.stride;
    for (uint32_t x = 0; x < fb.width; ++x) {
      row[x] = pixel;
    }
  }
}

void Present(const Surface& src, const Framebuffer& fb, const Viewport& vp) {
  // One scaled row is built in RAM, then copied `scale` times: the framebuffer only sees sequential bulk writes.
  uint32_t row[kScreenWidth * kMaxScale];
  const int scale = vp.scale;
  const size_t row_bytes = static_cast<size_t>(vp.width) * sizeof(uint32_t);
  for (int y = 0; y < src.height; ++y) {
    const Color* in = src.Row(y);
    uint32_t* out = row;
    for (int x = 0; x < src.width; ++x) {
      const uint32_t pixel = ToFramebuffer(in[x], fb.order);
      for (int k = 0; k < scale; ++k) {
        *out++ = pixel;
      }
    }
    for (int k = 0; k < scale; ++k) {
      const size_t fb_y = static_cast<size_t>(vp.y + y * scale + k);
      lib::Copy(fb.base + fb_y * fb.stride + vp.x, row, row_bytes);
    }
  }
}

}  // namespace gfx
