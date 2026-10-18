#include "game/enemy.h"

#include "game/player.h"
#include "lib/mem.h"

namespace game {
namespace {

constexpr Fixed kWalkSpeed = Fixed::FromRatio(3, 4);
constexpr Fixed kHopSpeed = Fixed::FromInt(1);
constexpr Fixed kHopJump = Fixed::FromRatio(9, 2);  // about 2 tiles high, 1.8 tiles far
constexpr Fixed kFlySpeed = Fixed::FromRatio(3, 4);
constexpr Fixed kGravity = PlayerTuning::kGravity;
constexpr Fixed kMaxFallSpeed = PlayerTuning::kMaxFallSpeed;
constexpr int kBobPeriodTicks = 64;
// One period of the flyer's bob in 16 steps: round(6 * sin(2 pi i / 16)) pixels.
constexpr int kBob[16] = {0, 2, 4, 6, 6, 6, 4, 2, 0, -2, -4, -6, -6, -6, -4, -2};

bool HitsSolid(const Level& level, int x, int y) { return BoxHitsSolid(level, x, y, Enemy::kWidth, Enemy::kHeight); }

// Solid ground under the column `columns` tiles ahead of the enemy's front edge (1 = the next tile).
bool GroundAhead(const Enemy& e, const Level& level, int columns) {
  const int front = e.dir > 0 ? e.pixel_x() + Enemy::kWidth - 1 : e.pixel_x();
  const int below = (e.pixel_y() + Enemy::kHeight) >> kTileShift;
  for (int i = 1; i <= columns; ++i) {
    const int tx = (front >> kTileShift) + e.dir * i;
    if (!level.IsSolid(tx, below)) {
      return false;
    }
  }
  return true;
}

// Horizontal move by `dx`; true if a wall stopped it.
bool MoveX(Enemy& e, const Level& level, Fixed dx) {
  const Fixed old = e.x;
  e.x += dx;
  if (HitsSolid(level, e.pixel_x(), e.pixel_y())) {
    e.x = old;
    return true;
  }
  return false;
}

// Gravity and landing. Speeds stay below a tile per tick, so snapping to the tile edge is enough.
void Fall(Enemy& e, const Level& level) {
  e.vy = lib::Min(e.vy + kGravity, kMaxFallSpeed);
  e.y += e.vy;
  e.on_ground = false;
  if (HitsSolid(level, e.pixel_x(), e.pixel_y())) {
    if (e.vy > Fixed()) {
      e.y = Fixed::FromInt(((e.pixel_y() + Enemy::kHeight) >> kTileShift << kTileShift) - Enemy::kHeight);
      e.on_ground = true;
    } else {
      e.y = Fixed::FromInt(((e.pixel_y() >> kTileShift) + 1) << kTileShift);
    }
    e.vy = Fixed();
  } else if (HitsSolid(level, e.pixel_x(), e.pixel_y() + 1) && e.vy >= Fixed()) {
    e.on_ground = true;
  }
}

}  // namespace

void Enemy::Spawn(EnemyKind k, int tx, int ty) {
  kind = k;
  spawn_x = tx * kTileSize + (kTileSize - kWidth) / 2;
  spawn_y = kind == EnemyKind::kFlyer ? ty * kTileSize + (kTileSize - kHeight) / 2 : (ty + 1) * kTileSize - kHeight;
  Reset();
}

void Enemy::Reset() {
  x = Fixed::FromInt(spawn_x);
  y = Fixed::FromInt(spawn_y);
  vx = vy = Fixed();
  dir = -1;
  on_ground = false;
  timer = kind == EnemyKind::kJumper ? kJumpIntervalTicks : 0;
  ticks = 0;
}

void UpdateEnemy(Enemy& e, const Level& level) {
  ++e.ticks;
  switch (e.kind) {
    case EnemyKind::kWalker:
      if (e.on_ground && !GroundAhead(e, level, 1)) {
        e.dir = -e.dir;
      }
      if (MoveX(e, level, kWalkSpeed * e.dir)) {
        e.dir = -e.dir;
      }
      Fall(e, level);
      break;
    case EnemyKind::kJumper:
      if (e.on_ground) {
        e.vx = Fixed();
        if (--e.timer <= 0) {
          // A hop covers about two tiles: turn first if they are not both ground.
          if (!GroundAhead(e, level, 2) || HitsSolid(level, e.pixel_x() + e.dir * kTileSize, e.pixel_y())) {
            e.dir = -e.dir;
          }
          e.vx = kHopSpeed * e.dir;
          e.vy = -kHopJump;
          e.timer = Enemy::kJumpIntervalTicks;
        }
      }
      if (MoveX(e, level, e.vx)) {
        e.vx = Fixed();
        e.dir = -e.dir;
      }
      Fall(e, level);
      break;
    case EnemyKind::kFlyer: {
      const int offset = e.pixel_x() - e.spawn_x;
      if ((e.dir > 0 && offset >= Enemy::kFlyerRange) || (e.dir < 0 && offset <= -Enemy::kFlyerRange) ||
          MoveX(e, level, kFlySpeed * e.dir)) {
        e.dir = -e.dir;
      }
      e.timer = (e.timer + 1) % kBobPeriodTicks;
      const int bob = kBob[e.timer * 16 / kBobPeriodTicks];
      if (!HitsSolid(level, e.pixel_x(), e.spawn_y + bob)) {
        e.y = Fixed::FromInt(e.spawn_y + bob);
      }
      break;
    }
  }
  // Fell out of the level (a jumper misjudging a pit): back to the start.
  if (e.pixel_y() > level.pixel_height() + 32) {
    e.Reset();
  }
}

bool EnemyTouches(const Enemy& e, int x, int y, int width, int height) {
  constexpr int kInset = 2;
  const int ex = e.pixel_x() + kInset, ey = e.pixel_y() + kInset;
  const int ew = Enemy::kWidth - 2 * kInset, eh = Enemy::kHeight - 2 * kInset;
  return ex < x + width && x < ex + ew && ey < y + height && y < ey + eh;
}

}  // namespace game
