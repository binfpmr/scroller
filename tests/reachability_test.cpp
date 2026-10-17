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
  test::TestLevel t(
      "#........#\n"
      "#.####...#\n"
      "#........#\n"
      "#........#\n"
      "#........#\n"
      "#P.......#\n"
      "##########\n");
  const levelcheck::Reachability reach = levelcheck::Explore(t.level);
  EXPECT_TRUE(reach.StandsOn(7, 6));
  EXPECT_TRUE(!reach.StandsOn(3, 1));  // 5 tiles up: too high
}
