#include "gfx/present.h"

#include <vector>

#include "test.h"

namespace {

struct Fixture {
  std::vector<gfx::Color> screen_pixels = std::vector<gfx::Color>(gfx::kScreenWidth * gfx::kScreenHeight);
  gfx::Surface screen{screen_pixels.data(), gfx::kScreenWidth, gfx::kScreenHeight};
  std::vector<uint32_t> fb_pixels;
  gfx::Framebuffer fb;

  Fixture(uint32_t width, uint32_t height, uint32_t stride, gfx::PixelOrder order) : fb_pixels(stride * height) {
    fb = {.base = fb_pixels.data(), .width = width, .height = height, .stride = stride, .order = order};
  }
  uint32_t At(uint32_t x, uint32_t y) const { return fb_pixels[y * fb.stride + x]; }
};

}  // namespace

TEST(PresentScalesAndOffsets) {
  Fixture f(700, 500, 704, gfx::PixelOrder::kBgrx);  // scale 2, borders 30/10, padded stride
  f.screen.Row(0)[0] = gfx::Rgb(1, 2, 3);
  f.screen.Row(239)[319] = gfx::Rgb(4, 5, 6);
  const gfx::Viewport vp = gfx::ComputeViewport(f.fb.width, f.fb.height);
  EXPECT_EQ(vp.scale, 2);
  gfx::ClearFramebuffer(f.fb, gfx::Rgb(9, 9, 9));
  gfx::Present(f.screen, f.fb, vp);

  EXPECT_EQ(f.At(29, 10), gfx::Rgb(9, 9, 9));  // left border untouched
  for (uint32_t dy = 0; dy < 2; ++dy) {
    for (uint32_t dx = 0; dx < 2; ++dx) {
      EXPECT_EQ(f.At(30 + dx, 10 + dy), gfx::Rgb(1, 2, 3));
      EXPECT_EQ(f.At(30 + 638 + dx, 10 + 478 + dy), gfx::Rgb(4, 5, 6));
    }
  }
  EXPECT_EQ(f.At(30 + 640, 10), gfx::Rgb(9, 9, 9));  // right border untouched
  EXPECT_EQ(f.At(30, 10 + 480), gfx::Rgb(9, 9, 9));  // bottom border untouched
}

TEST(PresentSwapsRedAndBlueForRgbx) {
  Fixture f(320, 240, 320, gfx::PixelOrder::kRgbx);
  f.screen.Row(0)[0] = gfx::Rgb(0x11, 0x22, 0x33);
  gfx::Present(f.screen, f.fb, gfx::ComputeViewport(320, 240));
  EXPECT_EQ(f.At(0, 0), 0x00332211u);
}
