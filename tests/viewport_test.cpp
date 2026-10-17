#include "gfx/viewport.h"

#include "test.h"

using gfx::ChooseVideoMode;
using gfx::ComputeViewport;
using gfx::VideoMode;

TEST(ViewportExactFit) {
  const gfx::Viewport vp = ComputeViewport(960, 720);
  EXPECT_EQ(vp.scale, 3);
  EXPECT_EQ(vp.x, 0);
  EXPECT_EQ(vp.y, 0);
  EXPECT_EQ(vp.width, 960);
  EXPECT_EQ(vp.height, 720);
}

TEST(ViewportLargestIntegerScaleCentred) {
  const gfx::Viewport vp = ComputeViewport(1920, 1080);  // 4.5x vertically -> 4
  EXPECT_EQ(vp.scale, 4);
  EXPECT_EQ(vp.x, (1920 - 1280) / 2);
  EXPECT_EQ(vp.y, (1080 - 960) / 2);
  EXPECT_EQ(ComputeViewport(3840, 2160).scale, 9);
  EXPECT_EQ(ComputeViewport(3072, 1920).scale, 8);
}

TEST(ViewportScaleIsCapped) { EXPECT_EQ(ComputeViewport(20000, 20000).scale, gfx::kMaxScale); }

TEST(ViewportTooSmallStillScaleOne) {
  const gfx::Viewport vp = ComputeViewport(320, 200);
  EXPECT_EQ(vp.scale, 1);
  EXPECT_EQ(vp.y, 0);
}

TEST(ChooseKeepsUsableCurrentMode) {
  // Native 1920x1080 is current: keep it although 2560x1440 or an exact 1280x960 exist.
  const VideoMode modes[] = {{1280, 960, true}, {2560, 1440, true}, {1920, 1080, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 2), 2);
  const VideoMode qemu[] = {{960, 720, true}, {640, 480, true}, {2560, 1600, true}};
  EXPECT_EQ(ChooseVideoMode(qemu, 3, 0), 0);
}

TEST(ChooseOtherModeIfCurrentUnusable) {
  const VideoMode modes[] = {{1920, 1080, false}, {640, 480, true}, {1024, 768, true}, {800, 600, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 4, 0), 2);  // 1024x768 -> 3x
  const VideoMode exact[] = {{320, 200, true}, {1280, 800, true}, {960, 720, true}};
  EXPECT_EQ(ChooseVideoMode(exact, 3, 0), 2);  // current too small; both others 3x, 960x720 exact
}

TEST(ChooseSkipsUnusableAndTooSmall) {
  const VideoMode modes[] = {{1280, 960, false}, {320, 200, true}, {640, 480, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 0), 2);
  const VideoMode none[] = {{1280, 960, false}, {300, 200, true}};
  EXPECT_EQ(ChooseVideoMode(none, 2, 0), -1);
}
