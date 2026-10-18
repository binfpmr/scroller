#include "game/game.h"

#include <cstring>
#include <vector>

#include "game_test_util.h"

using platform::Key;

// Plays the real test level with pseudo-random input and checks invariants every tick.
TEST(RandomPlayKeepsInvariants) {
  std::vector<uint8_t> storage(1 << 20);
  lib::Arena arena(storage.data(), storage.size());
  game::Game g;
  const char* error = nullptr;
  const std::string level = test::ReadAsset("levels/test.txt");
  const test::TestAssets assets;
  EXPECT_TRUE(g.Init(arena, level.data(), level.size(), assets.assets(), &error));

  std::vector<uint32_t> screen_pixels(gfx::kScreenWidth * gfx::kScreenHeight);
  gfx::Surface screen{screen_pixels.data(), gfx::kScreenWidth, gfx::kScreenHeight};
  uint32_t rng = 12345;
  platform::InputTracker tracker;
  int min_x = 1 << 30, max_x = 0, min_y = 1 << 30;
  for (int tick = 0; tick < 20000; ++tick) {
    if (tick % 15 == 0) {
      rng = rng * 1664525u + 1013904223u;
      tracker.OnKey(Key::kLeft, (rng >> 8) % 4 == 0);
      tracker.OnKey(Key::kRight, (rng >> 12) % 3 != 0);
      tracker.OnKey(Key::kJump, (rng >> 16) % 2 == 0);
      tracker.OnKey(Key::kDown, (rng >> 20) % 5 == 0);
    }
    g.Update(tracker.NextTick());
    const game::Player& p = g.player();
    const game::Camera& c = g.camera();
    if (game::BoxHitsSolid(g.level(), p.pixel_x(), p.pixel_y(), game::Player::kWidth, game::Player::kHeight)) {
      test::Fail(__FILE__, __LINE__, "player overlaps a solid tile");
      return;
    }
    EXPECT_TRUE(c.x >= 0 && c.x <= g.level().pixel_width() - gfx::kScreenWidth);
    EXPECT_TRUE(c.y >= 0 && c.y <= g.level().pixel_height() - gfx::kScreenHeight);
    min_x = std::min(min_x, p.pixel_x());
    max_x = std::max(max_x, p.pixel_x());
    min_y = std::min(min_y, p.pixel_y());
    if (tick % 1000 == 0) {
      g.Render(screen);  // must not crash at any camera position
    }
  }
  // The random walk should have explored a good part of the level.
  EXPECT_TRUE(max_x - min_x > 400);
  EXPECT_TRUE(min_y < 33 * 16 - 64);
}

// Reaching the goal shows the banner, then the next level starts; after the last one the first comes again.
TEST(GoalAdvancesToTheNextLevel) {
  std::vector<uint8_t> storage(1 << 20);
  lib::Arena arena(storage.data(), storage.size());
  const char* one = "..........\n.P.G......\n##########\n";
  const char* two = "............\n.P...G......\n############\n";
  const game::LevelText levels[] = {{one, std::strlen(one)}, {two, std::strlen(two)}};
  const test::TestAssets assets;
  game::Game g;
  const char* error = nullptr;
  EXPECT_TRUE(g.Init(arena, levels, 2, assets.assets(), &error));
  EXPECT_EQ(g.level_count(), 2);
  for (int expected : {0, 1, 0}) {
    EXPECT_EQ(g.level_index(), expected);
    int ticks = 0;
    while (!g.won() && ticks++ < 600) {
      g.Update(test::Keys({Key::kRight}));
    }
    EXPECT_TRUE(g.won());
    for (int i = 0; i < game::Game::kWinTicks; ++i) {
      g.Update({});
    }
    EXPECT_TRUE(!g.won());
  }
}

// Walking off into a pit (below the map is open) respawns at the start; after touching a checkpoint, there.
TEST(PitsRespawnAtTheLastCheckpoint) {
  std::vector<uint8_t> storage(1 << 20);
  lib::Arena arena(storage.data(), storage.size());
  const char* level =
      "..............\n"
      "..............\n"
      ".P.....C......\n"
      "#########...##\n";
  const test::TestAssets assets;
  game::Game g;
  const char* error = nullptr;
  EXPECT_TRUE(g.Init(arena, level, std::strlen(level), assets.assets(), &error));
  const int start_x = g.player().pixel_x();

  // Run right past the checkpoint and into the pit.
  bool checkpoint = false, fell = false;
  for (int i = 0; i < 600 && !fell; ++i) {
    g.Update(test::Keys({Key::kRight}));
    const uint8_t events = g.TakeEvents();
    checkpoint |= (events & game::Game::kEventCheckpoint) != 0;
    fell = (events & game::Game::kEventFall) != 0;
  }
  EXPECT_TRUE(checkpoint && fell);
  EXPECT_EQ(g.respawn_tx(), 7);
  EXPECT_EQ(g.player().pixel_x() >> game::kTileShift, 7);  // back at the checkpoint, not the start
  EXPECT_TRUE(g.player().pixel_x() != start_x);

  // Touching the same checkpoint again is no new event.
  g.Update({});
  EXPECT_EQ(g.TakeEvents() & game::Game::kEventCheckpoint, 0);

  // Esc (Restart) forgets the checkpoint.
  g.RestartFromFirstLevel();
  EXPECT_EQ(g.respawn_tx(), 1);
}
