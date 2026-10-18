// The player: platformer movement with tile collision.
#pragma once

#include "game/level.h"
#include "lib/fixed.h"
#include "platform/input.h"

namespace game {

using lib::Fixed;

// Movement tuning, per 60 Hz tick. Jump height is about 3.4 tiles (platforms 3 tiles up are reachable), a duck-jump
// about 5.1 tiles.
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

  // Duck-jump: jumping while Down is held on the ground, or within kDuckJumpWindowTicks after releasing it, starts
  // with kJumpSpeed * sqrt(1.5): 50% more height.
  static constexpr Fixed kDuckJumpSpeed = Fixed::FromRatio(7348, 1000);
  static constexpr int kDuckJumpWindowTicks = 10;

  // Wall-jump: jumping in the air while touching a side wall, or up to kWallCoyoteTicks after, starts a full jump
  // from zero horizontal speed. Until the top of that jump the input is overridden as if jump and the direction away
  // from the wall were held (no jump cut, no steering back).
  static constexpr int kWallCoyoteTicks = 6;
};

// What happened during the last UpdatePlayer, for sounds (bit mask).
enum PlayerEvent : uint8_t {
  kEventJump = 1 << 0,
  kEventWallJump = 1 << 1,
  kEventLand = 1 << 2,  // touched down after falling faster than 2 px per tick
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
  bool ducking = false;  // Down held on the ground (only changes the look and enables the duck-jump)
  int ticks_since_ground = PlayerTuning::kCoyoteTicks + 1;
  int ticks_since_jump_pressed = PlayerTuning::kJumpBufferTicks + 1;
  int ticks_since_duck = PlayerTuning::kDuckJumpWindowTicks + 1;
  int ticks_since_wall = PlayerTuning::kWallCoyoteTicks + 1;
  int wall_side = 0;           // -1 wall on the left, +1 on the right (last wall touched in the air)
  int wall_jump_away_dir = 0;  // non-zero while a wall-jump overrides the input
  uint8_t events = 0;          // PlayerEvent bits from the last update

  int pixel_x() const { return x.Floor(); }
  int pixel_y() const { return y.Floor(); }

  // Places the player standing on the bottom of tile (tx, ty), centered horizontally.
  void Spawn(int tx, int ty);
};

void UpdatePlayer(Player& player, const platform::InputState& input, const Level& level);

// True if any tile under the pixel box is solid.
bool BoxHitsSolid(const Level& level, int x, int y, int width, int height);

}  // namespace game
