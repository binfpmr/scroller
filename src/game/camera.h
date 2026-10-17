// Camera: top-left corner of the visible 320x240 window, in world pixels.
#pragma once

namespace game {

struct Camera {
  static constexpr int kDeadZoneHalfWidth = 24;  // target may move this far from the screen centre freely
  static constexpr int kDeadZoneHalfHeight = 24;

  int x = 0;
  int y = 0;

  // Keeps (target_x, target_y) inside the dead zone around the screen centre, then clamps to the level. A level
  // smaller than the screen is centred.
  void Follow(int target_x, int target_y, int level_width, int level_height);
  // Puts the target exactly in the centre (clamped), e.g. on spawn.
  void CenterOn(int target_x, int target_y, int level_width, int level_height);
};

}  // namespace game
