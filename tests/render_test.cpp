#include "game/render.h"

#include "game_test_util.h"

using game::Player;

TEST(PlayerFrameFollowsState) {
  const test::TestAssets assets;
  const gfx::SpriteSheet& sheet = assets.player;
  Player p;
  p.on_ground = true;
  game::Frames frames{};
  frames.idle = sheet.Find("idle");
  frames.blink = sheet.Find("blink");
  for (int i = 0; i < 4; ++i) {
    frames.run[i] = sheet.Find(("run" + std::to_string(i + 1)).c_str());
  }
  frames.jump = sheet.Find("jump");
  frames.fall = sheet.Find("fall");
  frames.duck = sheet.Find("duck");

  EXPECT_TRUE(game::PlayerFrame(p, frames, 100) == frames.idle);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 200) == frames.blink);
  p.vx = lib::Fixed::FromInt(1);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 0) == frames.run[0]);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 6) == frames.run[1]);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 24) == frames.run[0]);
  p.on_ground = false;
  p.vy = lib::Fixed::FromInt(-2);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 0) == frames.jump);
  p.vy = lib::Fixed::FromInt(2);
  EXPECT_TRUE(game::PlayerFrame(p, frames, 0) == frames.fall);
  p.on_ground = true;
  p.ducking = true;
  EXPECT_TRUE(game::PlayerFrame(p, frames, 0) == frames.duck);
}

TEST(TouchingTheGoalWinsAndRestarts) {
  const test::TestAssets assets;
  std::vector<uint8_t> storage(1 << 16);
  lib::Arena arena(storage.data(), storage.size());
  game::Game g;
  const char* error = nullptr;
  const char* level = "#......#\n#P...G.#\n########\n";
  EXPECT_TRUE(g.Init(arena, level, std::strlen(level), assets.assets(), &error));
  int ticks = 0;
  while (!g.won() && ticks < 300) {
    g.Update(test::Keys({platform::Key::kRight}));
    ++ticks;
  }
  EXPECT_TRUE(g.won());
  for (int i = 0; i < game::Game::kWinTicks; ++i) {
    g.Update(test::Keys({platform::Key::kLeft}));  // ignored while the banner shows
  }
  EXPECT_TRUE(!g.won());
  EXPECT_EQ(g.player().pixel_x(), 16 + 2);  // back at the start
}
