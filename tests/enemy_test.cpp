#include "game/enemy.h"

#include <cstring>

#include "game/game.h"
#include "game_test_util.h"

using game::Enemy;
using game::EnemyKind;
using platform::Key;

namespace {

// A platform with gaps on both sides, above a pit: enemies on it must never fall off.
const char* kPlatform =
    "P.............\n"
    "..............\n"
    "..............\n"
    "...######.....\n"
    "..............\n"
    "..............\n";

Enemy SpawnOn(const game::Level& level, EnemyKind kind, int tx, int ty) {
  Enemy e;
  e.Spawn(kind, tx, ty);
  for (int i = 0; i < 5; ++i) {
    game::UpdateEnemy(e, level);
  }
  return e;
}

}  // namespace

TEST(WalkerTurnsAtLedgesAndNeverFalls) {
  test::TestLevel t(kPlatform);
  Enemy e = SpawnOn(t.level, EnemyKind::kWalker, 5, 2);
  int min_x = 1 << 30, max_x = -1, turns = 0, dir = e.dir;
  for (int i = 0; i < 2000; ++i) {
    game::UpdateEnemy(e, t.level);
    EXPECT_TRUE(e.pixel_y() + Enemy::kHeight == 3 * game::kTileSize);  // always standing on the platform
    min_x = std::min(min_x, e.pixel_x());
    max_x = std::max(max_x, e.pixel_x());
    turns += e.dir != dir ? 1 : 0;
    dir = e.dir;
  }
  EXPECT_TRUE(min_x >= 3 * game::kTileSize - Enemy::kWidth && max_x <= 9 * game::kTileSize);
  EXPECT_TRUE(turns >= 4);  // walks the whole platform back and forth
}

TEST(WalkerTurnsAtWalls) {
  test::TestLevel t(
      "P.........\n"
      "..X....X..\n"
      "##########\n");
  Enemy e = SpawnOn(t.level, EnemyKind::kWalker, 4, 1);
  for (int i = 0; i < 1000; ++i) {
    game::UpdateEnemy(e, t.level);
    EXPECT_TRUE(e.pixel_x() >= 3 * game::kTileSize && e.pixel_x() + Enemy::kWidth <= 7 * game::kTileSize);
  }
}

TEST(JumperHopsButStaysOnItsPlatform) {
  test::TestLevel t(kPlatform);
  Enemy e = SpawnOn(t.level, EnemyKind::kJumper, 5, 2);
  bool hopped = false;
  for (int i = 0; i < 3000; ++i) {
    game::UpdateEnemy(e, t.level);
    hopped |= !e.on_ground;
    EXPECT_TRUE(e.pixel_y() < 3 * game::kTileSize);  // never below the platform's top
  }
  EXPECT_TRUE(hopped);
}

TEST(FlyerPatrolsAndBobs) {
  test::TestLevel t(
      "P...................\n"
      "....................\n"
      "....................\n"
      "....................\n"
      "####################\n");
  Enemy e = SpawnOn(t.level, EnemyKind::kFlyer, 10, 1);
  int min_x = 1 << 30, max_x = -1, min_y = 1 << 30, max_y = -1;
  for (int i = 0; i < 1000; ++i) {
    game::UpdateEnemy(e, t.level);
    min_x = std::min(min_x, e.pixel_x());
    max_x = std::max(max_x, e.pixel_x());
    min_y = std::min(min_y, e.pixel_y());
    max_y = std::max(max_y, e.pixel_y());
  }
  EXPECT_TRUE(max_x - min_x >= 2 * Enemy::kFlyerRange - 2 && max_x - min_x <= 2 * Enemy::kFlyerRange + 2);
  EXPECT_TRUE(max_y - min_y >= 10 && max_y - min_y <= 13);  // bobs about 6 px up and down
}

TEST(TouchingAnEnemyRespawnsThePlayer) {
  std::vector<uint8_t> storage(1 << 20);
  lib::Arena arena(storage.data(), storage.size());
  const char* level =
      "..............\n"
      ".P......w.....\n"
      "##############\n";
  const test::TestAssets assets;
  game::Game g;
  const char* error = nullptr;
  EXPECT_TRUE(g.Init(arena, level, std::strlen(level), assets.assets(), &error));
  EXPECT_EQ(g.enemy_count(), 1);
  const int start_x = g.player().pixel_x();
  bool hit = false;
  int max_x = 0;
  for (int i = 0; i < 600 && !hit; ++i) {
    g.Update(test::Keys({Key::kRight}));
    max_x = std::max(max_x, g.player().pixel_x());
    hit = (g.TakeEvents() & game::Game::kEventHit) != 0;
  }
  EXPECT_TRUE(hit);
  EXPECT_EQ(g.player().pixel_x(), start_x);
  EXPECT_TRUE(max_x > start_x + 32);
}
