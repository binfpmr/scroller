#include "game/player.h"

#include "game_test_util.h"

using game::Player;
using platform::InputState;
using platform::Key;

namespace {

Player SpawnAtStart(const game::Level& level) {
  Player p;
  p.Spawn(level.start_x, level.start_y);
  return p;
}

// Runs `ticks` updates with the same input; returns the highest point (smallest y) reached.
int Run(Player& p, const game::Level& level, const InputState& input, int ticks) {
  int top = p.pixel_y();
  for (int i = 0; i < ticks; ++i) {
    UpdatePlayer(p, input, level);
    top = top < p.pixel_y() ? top : p.pixel_y();
  }
  return top;
}

// Holds jump for one tick (pressed), then keeps it held for `hold_ticks` more and releases.
int JumpHeight(const game::Level& level, int hold_ticks) {
  Player p = SpawnAtStart(level);
  Run(p, level, {}, 5);  // settle on the floor
  const int floor_y = p.pixel_y();
  int top = Run(p, level, test::Keys({}, {Key::kJump}), 1);
  top = std::min(top, Run(p, level, test::Keys({Key::kJump}), hold_ticks));
  top = std::min(top, Run(p, level, {}, 120));
  return floor_y - top;
}

// Runs right from the start until x >= 40, then jumps (holding right and jump) and waits for the landing.
void RunUpAndJump(Player& p, const game::Level& level) {
  Run(p, level, {}, 5);
  while (p.pixel_x() < 40) {
    UpdatePlayer(p, test::Keys({Key::kRight}), level);
  }
  Run(p, level, test::Keys({Key::kRight, Key::kJump}, {Key::kJump}), 1);
  Run(p, level, test::Keys({Key::kRight, Key::kJump}), 120);
}

const char* kFlat =
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#...P....#\n"
    "##########\n";

}  // namespace

TEST(SettlesOnFloor) {
  test::TestLevel t(kFlat);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 30);
  EXPECT_TRUE(p.on_ground);
  EXPECT_EQ(p.pixel_y(), 6 * 16 - Player::kHeight);
}

TEST(FullJumpClearsThreeTilesButNotFour) {
  test::TestLevel t(kFlat);
  const int height = JumpHeight(t.level, 60);
  EXPECT_TRUE(height > 48);
  EXPECT_TRUE(height < 64);
}

TEST(ReleasingEarlyGivesLowerJump) {
  test::TestLevel t(kFlat);
  const int tap = JumpHeight(t.level, 0);
  const int full = JumpHeight(t.level, 60);
  EXPECT_TRUE(tap >= 10);
  EXPECT_TRUE(tap < full / 2);
}

TEST(StopsAtWalls) {
  test::TestLevel t(kFlat);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, test::Keys({Key::kLeft}), 120);
  EXPECT_EQ(p.pixel_x(), 16);
  EXPECT_TRUE(p.facing_left);
  Run(p, t.level, test::Keys({Key::kRight}), 240);
  EXPECT_EQ(p.pixel_x(), 9 * 16 - Player::kWidth);
}

TEST(RunSpeedIsCapped) {
  test::TestLevel t(kFlat);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  Run(p, t.level, test::Keys({Key::kRight}), 20);
  EXPECT_EQ(p.vx, game::PlayerTuning::kMaxRunSpeed);
}

TEST(HeadBumpsCeiling) {
  test::TestLevel t(
      "##########\n"
      "#...P....#\n"
      "##########\n");
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  const int top = Run(p, t.level, test::Keys({Key::kJump}, {Key::kJump}), 30);
  EXPECT_EQ(top, 16);  // just below the ceiling row
  EXPECT_TRUE(p.on_ground);
}

TEST(ReachesPlatformThreeTilesUp) {
  test::TestLevel t(
      "#.........#\n"
      "#.........#\n"
      "#.........#\n"
      "#....######\n"
      "#.........#\n"
      "#P........#\n"
      "###########\n");
  Player p = SpawnAtStart(t.level);
  RunUpAndJump(p, t.level);
  EXPECT_TRUE(p.on_ground);
  EXPECT_EQ(p.pixel_y(), 3 * 16 - Player::kHeight);
}

TEST(CannotReachPlatformFourTilesUp) {
  test::TestLevel t(
      "#.........#\n"
      "#.........#\n"
      "#....######\n"
      "#.........#\n"
      "#.........#\n"
      "#P........#\n"
      "###########\n");
  Player p = SpawnAtStart(t.level);
  RunUpAndJump(p, t.level);
  EXPECT_TRUE(p.on_ground);
  EXPECT_EQ(p.pixel_y(), 6 * 16 - Player::kHeight);
}

TEST(CoyoteTimeAllowsLateJump) {
  test::TestLevel t(
      "#........#\n"
      "#........#\n"
      "#P.......#\n"
      "###......#\n"
      "#........#\n"
      "#........#\n"
      "##########\n");
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  int airborne_ticks = 0;
  while (airborne_ticks < 3) {  // walk off the ledge and stay in the air for a few ticks
    UpdatePlayer(p, test::Keys({Key::kRight}), t.level);
    airborne_ticks = p.on_ground ? 0 : airborne_ticks + 1;
  }
  UpdatePlayer(p, test::Keys({Key::kRight}, {Key::kJump}), t.level);
  EXPECT_TRUE(p.vy < lib::Fixed());
}

TEST(JumpBufferedBeforeLandingFires) {
  test::TestLevel t(kFlat);
  Player p = SpawnAtStart(t.level);
  p.y -= lib::Fixed::FromInt(30);  // drop from above
  while (p.pixel_y() < 6 * 16 - Player::kHeight - 8) {
    UpdatePlayer(p, {}, t.level);
  }
  UpdatePlayer(p, test::Keys({}, {Key::kJump}), t.level);  // pressed while still falling
  bool jumped = false;
  for (int i = 0; i < 8 && !jumped; ++i) {
    UpdatePlayer(p, test::Keys({Key::kJump}), t.level);
    jumped = p.vy < lib::Fixed() && !p.on_ground;
  }
  EXPECT_TRUE(jumped);
}

TEST(StaysGroundedEveryTick) {
  // Regression: gravity moves less than a pixel per tick, which used to make a standing player airborne 2 of 3 ticks.
  test::TestLevel t(kFlat);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  for (int i = 0; i < 60; ++i) {
    UpdatePlayer(p, test::Keys({Key::kRight}), t.level);
    EXPECT_TRUE(p.on_ground);
  }
}
