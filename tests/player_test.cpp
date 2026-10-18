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

namespace {

// Jump height (pixels) when Down is released `release_ticks_before` ticks before the jump (-1: never ducked).
int JumpHeightAfterDuck(const game::Level& level, int release_ticks_before) {
  Player p = SpawnAtStart(level);
  Run(p, level, {}, 5);
  if (release_ticks_before >= 0) {
    Run(p, level, test::Keys({Key::kDown}), 10);
    Run(p, level, {}, release_ticks_before);
  }
  const int floor_y = p.pixel_y();
  int top = Run(p, level, test::Keys({}, {Key::kJump}), 1);
  top = std::min(top, Run(p, level, test::Keys({Key::kJump}), 120));
  return floor_y - top;
}

const char* kTall =
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#...P....#\n"
    "##########\n";

}  // namespace

TEST(DuckingOnlyOnTheGround) {
  test::TestLevel t(kTall);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  Run(p, t.level, test::Keys({Key::kDown}), 1);
  EXPECT_TRUE(p.ducking);
  Run(p, t.level, test::Keys({Key::kDown}, {Key::kJump}), 3);
  EXPECT_TRUE(!p.ducking && !p.on_ground);
}

TEST(DuckJumpIsHalfAgainAsHigh) {
  test::TestLevel t(kTall);
  const double normal = JumpHeightAfterDuck(t.level, -1);
  const double duck = JumpHeightAfterDuck(t.level, 0);
  EXPECT_TRUE(duck / normal > 1.45 && duck / normal < 1.55);
}

TEST(DuckJumpWindowAfterReleasingDown) {
  test::TestLevel t(kTall);
  const int normal = JumpHeightAfterDuck(t.level, -1);
  EXPECT_TRUE(JumpHeightAfterDuck(t.level, 9) > normal + 20);  // released 9 ticks ago: still a duck-jump
  EXPECT_EQ(JumpHeightAfterDuck(t.level, 12), normal);         // too late
}

namespace {

// Jumps from the start toward the right wall (holding right) and returns the player at its first airborne tick
// touching that wall.
Player JumpAgainstRightWall(const game::Level& level) {
  Player p = SpawnAtStart(level);
  Run(p, level, {}, 5);
  Run(p, level, test::Keys({Key::kRight}, {Key::kJump}), 1);
  for (int i = 0; i < 120 && p.ticks_since_wall != 0; ++i) {
    UpdatePlayer(p, test::Keys({Key::kRight, Key::kJump}), level);
  }
  return p;
}

const char* kWall =
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#........#\n"
    "#.....P..#\n"
    "##########\n";

}  // namespace

TEST(WallJumpPushesAwayFromWall) {
  test::TestLevel t(kWall);
  Player p = JumpAgainstRightWall(t.level);
  EXPECT_TRUE(!p.on_ground && p.ticks_since_wall == 0 && p.wall_side == 1);
  const int wall_x = p.pixel_x();
  // Press jump while still holding toward the wall: the wall-jump overrides that direction during the rise.
  UpdatePlayer(p, test::Keys({Key::kRight}, {Key::kJump}), t.level);
  EXPECT_TRUE(p.vy < lib::Fixed() && p.wall_jump_away_dir == -1 && p.facing_left);
  int top = p.pixel_y();
  while (p.vy < lib::Fixed()) {
    UpdatePlayer(p, test::Keys({Key::kRight}), t.level);  // jump released: no cut during a wall-jump
    top = std::min(top, p.pixel_y());
  }
  EXPECT_TRUE(p.pixel_x() < wall_x - 10);
  EXPECT_EQ(p.wall_jump_away_dir, 0);  // control returns at the top
}

TEST(WallJumpRisesLikeAFullJump) {
  test::TestLevel t(kWall);
  Player p = JumpAgainstRightWall(t.level);
  const int start_y = p.pixel_y();
  UpdatePlayer(p, test::Keys({}, {Key::kJump}), t.level);
  int top = p.pixel_y();
  while (p.vy < lib::Fixed()) {
    UpdatePlayer(p, {}, t.level);
    top = std::min(top, p.pixel_y());
  }
  EXPECT_TRUE(start_y - top > 48);
}

TEST(WallJumpCoyoteWindow) {
  test::TestLevel t(kWall);
  for (int delay : {4, 10}) {
    Player p = JumpAgainstRightWall(t.level);
    Run(p, t.level, test::Keys({Key::kLeft}), 1);  // leave the wall
    for (int i = 0; i < delay - 1; ++i) {
      UpdatePlayer(p, {}, t.level);
    }
    UpdatePlayer(p, test::Keys({}, {Key::kJump}), t.level);
    EXPECT_EQ(p.wall_jump_away_dir != 0, delay <= game::PlayerTuning::kWallCoyoteTicks);
  }
}

TEST(NoWallJumpAwayFromWalls) {
  test::TestLevel t(kTall);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  Run(p, t.level, test::Keys({}, {Key::kJump}), 1);
  Run(p, t.level, test::Keys({Key::kJump}), 15);  // in the air, far from walls
  UpdatePlayer(p, test::Keys({Key::kJump}, {Key::kJump}), t.level);
  EXPECT_EQ(p.wall_jump_away_dir, 0);
}

TEST(SoundEvents) {
  test::TestLevel t(kWall);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  EXPECT_EQ(p.events, 0);
  UpdatePlayer(p, test::Keys({}, {Key::kJump}), t.level);
  EXPECT_EQ(p.events, game::kEventJump);
  bool landed = false;
  for (int i = 0; i < 120 && !landed; ++i) {
    UpdatePlayer(p, {}, t.level);
    landed = (p.events & game::kEventLand) != 0;
  }
  EXPECT_TRUE(landed);
  Run(p, t.level, {}, 10);
  for (int i = 0; i < 10; ++i) {  // standing and walking do not "land"
    UpdatePlayer(p, test::Keys({Key::kLeft}), t.level);
    EXPECT_EQ(p.events, 0);
  }
  p = JumpAgainstRightWall(t.level);
  UpdatePlayer(p, test::Keys({}, {Key::kJump}), t.level);
  EXPECT_EQ(p.events, game::kEventWallJump);
}

TEST(DuckingStartsWithASoundEvent) {
  test::TestLevel t(kTall);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  UpdatePlayer(p, test::Keys({Key::kDown}), t.level);
  EXPECT_EQ(p.events, game::kEventDuck);
  UpdatePlayer(p, test::Keys({Key::kDown}), t.level);
  EXPECT_EQ(p.events, 0);  // only when it starts
}

TEST(HoldingDownTwoSecondsDuckJumpsByItself) {
  test::TestLevel t(kTall);
  const int duck_jump = JumpHeightAfterDuck(t.level, 0);
  Player p = SpawnAtStart(t.level);
  Run(p, t.level, {}, 5);
  const int floor_y = p.pixel_y();
  Run(p, t.level, test::Keys({Key::kDown}), game::PlayerTuning::kDuckAutoJumpTicks - 1);
  EXPECT_TRUE(p.on_ground);  // not yet
  UpdatePlayer(p, test::Keys({Key::kDown}), t.level);
  EXPECT_EQ(p.events & game::kEventJump, game::kEventJump);
  const int top = Run(p, t.level, test::Keys({Key::kDown}), 100);  // Down still held, jump never pressed
  EXPECT_EQ(floor_y - top, duck_jump);                             // as high as a held duck-jump
  EXPECT_TRUE(p.on_ground);

  // Still holding Down after landing: no second jump until Down is released.
  Run(p, t.level, test::Keys({Key::kDown}), game::PlayerTuning::kDuckAutoJumpTicks + 20);
  EXPECT_TRUE(p.on_ground);
  Run(p, t.level, {}, 1);
  Run(p, t.level, test::Keys({Key::kDown}), game::PlayerTuning::kDuckAutoJumpTicks + 1);
  EXPECT_TRUE(!p.on_ground);
}
