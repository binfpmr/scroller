#include "game/camera.h"

#include "gfx/surface.h"
#include "lib/mem.h"

namespace game {
namespace {

int ClampAxis(int value, int level_size, int screen_size) {
  if (level_size <= screen_size) {
    return (level_size - screen_size) / 2;
  }
  return lib::Clamp(value, 0, level_size - screen_size);
}

int FollowAxis(int camera, int target, int screen_size, int half_zone) {
  const int offset = target - (camera + screen_size / 2);
  if (offset > half_zone) {
    return camera + offset - half_zone;
  }
  if (offset < -half_zone) {
    return camera + offset + half_zone;
  }
  return camera;
}

}  // namespace

void Camera::Follow(int target_x, int target_y, int level_width, int level_height) {
  x = ClampAxis(FollowAxis(x, target_x, gfx::kScreenWidth, kDeadZoneHalfWidth), level_width, gfx::kScreenWidth);
  y = ClampAxis(FollowAxis(y, target_y, gfx::kScreenHeight, kDeadZoneHalfHeight), level_height, gfx::kScreenHeight);
}

void Camera::CenterOn(int target_x, int target_y, int level_width, int level_height) {
  x = ClampAxis(target_x - gfx::kScreenWidth / 2, level_width, gfx::kScreenWidth);
  y = ClampAxis(target_y - gfx::kScreenHeight / 2, level_height, gfx::kScreenHeight);
}

}  // namespace game
