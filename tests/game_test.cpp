#include "game/game.h"

#include <vector>

#include "assets/test_level.h"
#include "game_test_util.h"

using platform::Key;

// Plays the real test level with pseudo-random input and checks invariants every tick.
TEST(RandomPlayKeepsInvariants) {
  std::vector<uint8_t> storage(1 << 20);
  lib::Arena arena(storage.data(), storage.size());
  game::Game g;
  const char* error = nullptr;
  EXPECT_TRUE(g.Init(arena, kTestLevel, sizeof(kTestLevel) - 1, &error));

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
