#include "game/level.h"

#include <cstring>
#include <vector>

#include "test.h"

namespace {

bool Parse(const char* text, game::Level* level, const char** error) {
  static std::vector<uint8_t> storage(4096);
  lib::Arena arena(storage.data(), storage.size());
  return game::ParseLevel(text, std::strlen(text), arena, level, error);
}

}  // namespace

TEST(ParsesSizeTilesAndStart) {
  game::Level level;
  const char* error = nullptr;
  EXPECT_TRUE(Parse("#..#\n#P\r\n####", &level, &error));
  EXPECT_EQ(level.width, 4);
  EXPECT_EQ(level.height, 3);
  EXPECT_EQ(level.start_x, 1);
  EXPECT_EQ(level.start_y, 1);
  EXPECT_TRUE(level.IsSolid(0, 0) && !level.IsSolid(1, 0) && level.IsSolid(3, 0));
  EXPECT_TRUE(!level.IsSolid(2, 1) && !level.IsSolid(3, 1));  // short line padded with empty tiles
  EXPECT_TRUE(level.IsSolid(2, 2));
}

TEST(OutsideIsWallsAndOpenAboveAndBelow) {
  game::Level level;
  const char* error = nullptr;
  EXPECT_TRUE(Parse("P.\n..\n", &level, &error));
  EXPECT_EQ(level.height, 2);  // trailing newline does not add a row
  EXPECT_TRUE(level.IsSolid(-1, 0) && level.IsSolid(2, 0));
  EXPECT_TRUE(!level.IsSolid(0, -1));
  EXPECT_TRUE(!level.IsSolid(0, 2));  // below the map: a bottomless pit
}

TEST(ParsesTileKinds) {
  game::Level level;
  const char* error = nullptr;
  EXPECT_TRUE(Parse("#XG.PC", &level, &error));
  EXPECT_TRUE(level.At(0, 0) == game::Tile::kGround && level.At(1, 0) == game::Tile::kStone);
  EXPECT_TRUE(level.At(2, 0) == game::Tile::kGoal && !level.IsSolid(2, 0));
  EXPECT_TRUE(level.At(5, 0) == game::Tile::kCheckpoint && !level.IsSolid(5, 0));
  EXPECT_TRUE(level.IsSolid(0, 0) && level.IsSolid(1, 0));
}

TEST(RejectsBadLevels) {
  game::Level level;
  const char* error = nullptr;
  EXPECT_TRUE(!Parse("", &level, &error));
  EXPECT_TRUE(!Parse("##\n..", &level, &error));  // no start
  EXPECT_TRUE(!Parse("P.P", &level, &error));     // two starts
  EXPECT_TRUE(!Parse("P.x", &level, &error));
  EXPECT_STREQ(error, "unknown tile character");
}
