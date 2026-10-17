#include "game/player.h"

#include "lib/mem.h"

namespace game {
namespace {

using platform::Key;
using T = PlayerTuning;

Fixed Approach(Fixed value, Fixed target, Fixed step) {
  if (value < target) {
    return lib::Min(value + step, target);
  }
  return lib::Max(value - step, target);
}

bool ColumnHitsSolid(const Level& level, int tx, int top_ty, int bottom_ty) {
  for (int ty = top_ty; ty <= bottom_ty; ++ty) {
    if (level.IsSolid(tx, ty)) {
      return true;
    }
  }
  return false;
}

bool RowHitsSolid(const Level& level, int ty, int left_tx, int right_tx) {
  for (int tx = left_tx; tx <= right_tx; ++tx) {
    if (level.IsSolid(tx, ty)) {
      return true;
    }
  }
  return false;
}

// Speeds stay below one tile per tick, so a move crosses at most one tile boundary: checking the leading edge's tile
// column (or row) after the move is enough.
void MoveX(Player& p, const Level& level) {
  p.x += p.vx;
  const int left = p.pixel_x();
  const int top_ty = p.pixel_y() >> kTileShift;
  const int bottom_ty = (p.pixel_y() + Player::kHeight - 1) >> kTileShift;
  if (p.vx > Fixed()) {
    const int tx = (left + Player::kWidth - 1) >> kTileShift;
    if (ColumnHitsSolid(level, tx, top_ty, bottom_ty)) {
      p.x = Fixed::FromInt(tx * kTileSize - Player::kWidth);
      p.vx = Fixed();
    }
  } else if (p.vx < Fixed()) {
    const int tx = left >> kTileShift;
    if (ColumnHitsSolid(level, tx, top_ty, bottom_ty)) {
      p.x = Fixed::FromInt((tx + 1) * kTileSize);
      p.vx = Fixed();
    }
  }
}

void MoveY(Player& p, const Level& level) {
  p.y += p.vy;
  p.on_ground = false;
  const int top = p.pixel_y();
  const int left_tx = p.pixel_x() >> kTileShift;
  const int right_tx = (p.pixel_x() + Player::kWidth - 1) >> kTileShift;
  if (p.vy > Fixed()) {
    const int ty = (top + Player::kHeight - 1) >> kTileShift;
    const int below = top + Player::kHeight;  // first pixel row under the hitbox
    if (RowHitsSolid(level, ty, left_tx, right_tx)) {
      p.y = Fixed::FromInt(ty * kTileSize - Player::kHeight);
      p.vy = Fixed();
      p.on_ground = true;
    } else if ((below & (kTileSize - 1)) == 0 && RowHitsSolid(level, below >> kTileShift, left_tx, right_tx)) {
      // Resting on a tile: gravity moved less than a pixel. Drop the fraction, or the box would sink by subpixels
      // and count as airborne until it crosses into the floor tile.
      p.y = Fixed::FromInt(top);
      p.vy = Fixed();
      p.on_ground = true;
    }
  } else if (p.vy < Fixed()) {
    const int ty = top >> kTileShift;
    if (RowHitsSolid(level, ty, left_tx, right_tx)) {
      p.y = Fixed::FromInt((ty + 1) * kTileSize);
      p.vy = Fixed();  // bumped the head
    }
  }
}

}  // namespace

void Player::Spawn(int tx, int ty) {
  x = Fixed::FromInt(tx * kTileSize + (kTileSize - kWidth) / 2);
  y = Fixed::FromInt((ty + 1) * kTileSize - kHeight);
  vx = Fixed();
  vy = Fixed();
}

void UpdatePlayer(Player& p, const platform::InputState& input, const Level& level) {
  // Horizontal: accelerate toward the held direction, turn around with acceleration plus friction.
  const int dir = (input.Held(Key::kRight) ? 1 : 0) - (input.Held(Key::kLeft) ? 1 : 0);
  const Fixed accel = p.on_ground ? T::kGroundAccel : T::kAirAccel;
  const Fixed friction = p.on_ground ? T::kGroundFriction : T::kAirFriction;
  if (dir != 0) {
    const bool turning = (dir > 0 && p.vx < Fixed()) || (dir < 0 && p.vx > Fixed());
    p.vx = Approach(p.vx, T::kMaxRunSpeed * dir, turning ? accel + friction : accel);
    p.facing_left = dir < 0;
  } else {
    p.vx = Approach(p.vx, Fixed(), friction);
  }

  // Jump with buffering and coyote time; releasing the button early cuts the jump short.
  p.ticks_since_jump_pressed = input.Pressed(Key::kJump) ? 0 : lib::Min(p.ticks_since_jump_pressed + 1, 1000);
  p.ticks_since_ground = p.on_ground ? 0 : lib::Min(p.ticks_since_ground + 1, 1000);
  if (p.ticks_since_jump_pressed <= T::kJumpBufferTicks && p.ticks_since_ground <= T::kCoyoteTicks) {
    p.vy = -T::kJumpSpeed;
    p.ticks_since_jump_pressed = T::kJumpBufferTicks + 1;
    p.ticks_since_ground = T::kCoyoteTicks + 1;
  }
  if (!input.Held(Key::kJump) && p.vy < -T::kJumpCutSpeed) {
    p.vy = -T::kJumpCutSpeed;
  }

  p.vy = lib::Min(p.vy + T::kGravity, T::kMaxFallSpeed);
  MoveX(p, level);
  MoveY(p, level);
}

bool BoxHitsSolid(const Level& level, int x, int y, int width, int height) {
  for (int ty = y >> kTileShift; ty <= (y + height - 1) >> kTileShift; ++ty) {
    if (RowHitsSolid(level, ty, x >> kTileShift, (x + width - 1) >> kTileShift)) {
      return true;
    }
  }
  return false;
}

}  // namespace game
