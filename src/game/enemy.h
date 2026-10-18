// Enemies: obstacles to avoid (they cannot be defeated). Touching one sends the player back to the last checkpoint.
//   walker: walks along the ground, turns around at walls and ledges (never falls off)
//   jumper: hops along every ~1.5 s, turns at walls and before ledges
//   flyer:  flies back and forth (turning at walls or after kFlyerRange pixels), bobbing up and down, no gravity
#pragma once

#include <stdint.h>

#include "game/level.h"
#include "lib/fixed.h"

namespace game {

using lib::Fixed;

struct Enemy {
  static constexpr int kWidth = 12;  // hitbox; drawn as a 16x16 sprite centred on its bottom edge
  static constexpr int kHeight = 12;
  static constexpr int kFlyerRange = 48;  // pixels each way from the spawn point
  static constexpr int kJumpIntervalTicks = 90;

  EnemyKind kind = EnemyKind::kWalker;
  Fixed x;  // top-left corner of the hitbox
  Fixed y;
  Fixed vx;
  Fixed vy;
  int dir = 1;  // 1 right, -1 left
  bool on_ground = false;
  int timer = 0;    // jumper: ticks to the next hop; flyer: phase of the bob
  int spawn_x = 0;  // pixels (hitbox top-left at spawn)
  int spawn_y = 0;
  uint32_t ticks = 0;  // for animation

  // Standing on the bottom of tile (tx, ty) (a flyer: in its middle), centred, facing left.
  void Spawn(EnemyKind kind, int tx, int ty);
  void Reset();  // back to the spawn point and state

  int pixel_x() const { return x.Floor(); }
  int pixel_y() const { return y.Floor(); }
};

void UpdateEnemy(Enemy& enemy, const Level& level);

// True if the enemy touches the box (x, y, width, height). The enemy's box is shrunk by 2 pixels on every side, so a
// touch has to look like one.
bool EnemyTouches(const Enemy& enemy, int x, int y, int width, int height);

}  // namespace game
