#include "reachability.h"

#include "assets/test_level.h"
#include "game_test_util.h"

TEST(TestLevelIsFullyClimbable) {
  test::TestLevel t(kTestLevel);
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(40, 6));   // top walkway
  EXPECT_TRUE(reach.StandsOn(53, 7));   // top of the right-hand climb
  EXPECT_TRUE(reach.StandsOn(5, 15));   // stairs back down on the left
  EXPECT_TRUE(reach.StandsOn(35, 26));  // pillar top
}

TEST(UnreachableLedgeIsReported) {
  // Ledge in the middle, 7 tiles above the floor: higher than a duck-jump, too far from the walls to wall-jump to.
  test::TestLevel t(
      "#............#\n"
      "#....####....#\n"
      "#............#\n"
      "#............#\n"
      "#............#\n"
      "#............#\n"
      "#............#\n"
      "#P...........#\n"
      "##############\n");
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(12, 8));
  EXPECT_TRUE(!reach.StandsOn(6, 1));
}

TEST(DuckJumpReachesFourTilesUp) {
  test::TestLevel t(
      "#..........#\n"
      "#..........#\n"
      "#.....######\n"
      "#..........#\n"
      "#..........#\n"
      "#P.........#\n"
      "############\n");
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(8, 2));
}

TEST(WallJumpClimbsAShaft) {
  // A 2-tile wide shaft, 6 tiles deep: only wall-jumps get out.
  test::TestLevel t(
      "#.......#\n"
      "####..###\n"
      "#..#..#.#\n"
      "#..#..#.#\n"
      "#..#..#.#\n"
      "#..#..#.#\n"
      "#..#..#.#\n"
      "#..#P.#.#\n"
      "#########\n");
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(1, 1));
}
