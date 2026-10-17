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

TEST(ViewportCentersWithBorders) {
  const gfx::Viewport vp = ComputeViewport(1024, 768);  // 3.2x -> 3
  EXPECT_EQ(vp.scale, 3);
  EXPECT_EQ(vp.x, (1024 - 960) / 2);
  EXPECT_EQ(vp.y, (768 - 720) / 2);
}

TEST(ViewportScaleIsCapped) { EXPECT_EQ(ComputeViewport(3840, 2160).scale, gfx::kMaxScale); }

TEST(ViewportTooSmallStillScaleOne) {
  const gfx::Viewport vp = ComputeViewport(320, 200);
  EXPECT_EQ(vp.scale, 1);
  EXPECT_EQ(vp.y, 0);
}

TEST(ChoosePrefersHigherScale) {
  const VideoMode modes[] = {{640, 480, true}, {1024, 768, true}, {800, 600, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 0), 1);  // 1024x768 -> 3x
}

TEST(ChoosePrefersExactFitAtSameScale) {
  const VideoMode modes[] = {{1280, 800, true}, {960, 720, true}, {1920, 1080, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 0), 1);
}

TEST(ChoosePrefersCurrentModeOverOtherNonExact) {
  // Laptop: native 1920x1080 is current; 2560x1440 would also give the capped 3x but is not native.
  const VideoMode modes[] = {{640, 480, true}, {2560, 1440, true}, {1920, 1080, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 2), 2);
}

TEST(ChooseSkipsUnusableAndTooSmall) {
  const VideoMode modes[] = {{1280, 960, false}, {320, 200, true}, {640, 480, true}};
  EXPECT_EQ(ChooseVideoMode(modes, 3, 0), 2);
  const VideoMode none[] = {{1280, 960, false}, {300, 200, true}};
  EXPECT_EQ(ChooseVideoMode(none, 2, 0), -1);
}
