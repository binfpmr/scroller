#include "reachability.h"

#include <string>

#include "game_test_util.h"

TEST(TestLevelIsFullyClimbable) {
  test::TestLevel t(test::ReadAsset("levels/test.txt").c_str());
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(40, 6));   // top walkway
  EXPECT_TRUE(reach.StandsOn(53, 7));   // top of the right-hand climb
  EXPECT_TRUE(reach.StandsOn(5, 15));   // stairs back down on the left
  EXPECT_TRUE(reach.StandsOn(35, 26));  // pillar top
}

TEST(UnreachableLedgeIsReported) {
  // Ledge 7 tiles up in the middle of a wide room: above a duck-jump, and too far from the walls for a duck-jump plus
  // wall-jump to drift onto it.
  test::TestLevel t(
      "#....................#\n"
      "#........####........#\n"
      "#....................#\n"
      "#....................#\n"
      "#....................#\n"
      "#....................#\n"
      "#....................#\n"
      "#P...................#\n"
      "######################\n");
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(20, 8));
  EXPECT_TRUE(!reach.StandsOn(10, 1));
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

TEST(DeadEndPitIsReported) {
  // A pit 12 tiles deep and 6 wide: a duck-jump plus a wall-jump reaches ~8.5 tiles, and the walls are too far apart
  // to chain wall-jumps. Falling in is possible, getting out is not.
  std::string text = "#................#\n#................#\n#P.............G.#\n";
  for (int i = 0; i < 12; ++i) {
    text += "#####......#######\n";
  }
  text += "##################\n";
  test::TestLevel trap(text.c_str());
  const levelcheck::Reachability reach = levelcheck::Explore(trap.level);
  EXPECT_TRUE(reach.ReachesGoal(trap.level));
  EXPECT_TRUE(reach.HasDeadEnd(trap.level));
  test::TestLevel open(
      "#................#\n"
      "#P.............G.#\n"
      "##################\n");
  EXPECT_TRUE(!levelcheck::Explore(open.level).HasDeadEnd(open.level));
}
