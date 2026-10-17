#include "game/camera.h"

#include "test.h"

using game::Camera;

TEST(DeadZoneKeepsCameraStill) {
  Camera c{.x = 100, .y = 100};
  c.Follow(100 + 160 + 20, 100 + 120 - 20, 2000, 2000);
  EXPECT_EQ(c.x, 100);
  EXPECT_EQ(c.y, 100);
}

TEST(CameraFollowsBeyondDeadZone) {
  Camera c{.x = 100, .y = 100};
  c.Follow(100 + 160 + 30, 100 + 120 - 40, 2000, 2000);
  EXPECT_EQ(c.x, 106);
  EXPECT_EQ(c.y, 84);
}

TEST(CameraClampsToLevel) {
  Camera c;
  c.Follow(5, 5, 1024, 576);
  EXPECT_EQ(c.x, 0);
  EXPECT_EQ(c.y, 0);
  c.CenterOn(1020, 570, 1024, 576);
  EXPECT_EQ(c.x, 1024 - 320);
  EXPECT_EQ(c.y, 576 - 240);
}

TEST(SmallLevelIsCentered) {
  Camera c;
  c.CenterOn(50, 50, 160, 120);
  EXPECT_EQ(c.x, -80);
  EXPECT_EQ(c.y, -60);
}
