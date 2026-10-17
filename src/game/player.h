// The player: platformer movement with tile collision.
#pragma once

#include "game/level.h"
#include "lib/fixed.h"
#include "platform/input.h"

namespace game {

using lib::Fixed;

// Movement tuning, per 60 Hz tick. Jump height is about 3.4 tiles, so platforms 3 tiles up are reachable.
struct PlayerTuning {
  static constexpr Fixed kMaxRunSpeed = Fixed::FromInt(2);
  static constexpr Fixed kGroundAccel = Fixed::FromRatio(1, 4);
  static constexpr Fixed kGroundFriction = Fixed::FromRatio(3, 10);
  static constexpr Fixed kAirAccel = Fixed::FromRatio(3, 20);
  static constexpr Fixed kAirFriction = Fixed::FromRatio(1, 20);
  static constexpr Fixed kGravity = Fixed::FromRatio(8, 25);  // 0.32
  static constexpr Fixed kMaxFallSpeed = Fixed::FromInt(6);   // must stay below kTileSize: no tunnelling
  static constexpr Fixed kJumpSpeed = Fixed::FromInt(6);
  static constexpr Fixed kJumpCutSpeed = Fixed::FromInt(3);  // upward speed left when jump is released early
  static constexpr int kCoyoteTicks = 6;                     // jump still allowed this long after leaving a ledge
  static constexpr int kJumpBufferTicks = 6;                 // jump pressed this long before landing still counts
};

struct Player {
  static constexpr int kWidth = 12;  // hitbox in pixels
  static constexpr int kHeight = 14;

  Fixed x;  // top-left corner of the hitbox, in pixels
  Fixed y;
  Fixed vx;
  Fixed vy;
  bool on_ground = false;
  bool facing_left = false;
  int ticks_since_ground = PlayerTuning::kCoyoteTicks + 1;
  int ticks_since_jump_pressed = PlayerTuning::kJumpBufferTicks + 1;

  int pixel_x() const { return x.Floor(); }
  int pixel_y() const { return y.Floor(); }

  // Places the player standing on the bottom of tile (tx, ty), centered horizontally.
  void Spawn(int tx, int ty);
};

void UpdatePlayer(Player& player, const platform::InputState& input, const Level& level);

// True if any tile under the pixel box is solid.
bool BoxHitsSolid(const Level& level, int x, int y, int width, int height);

}  // namespace game
