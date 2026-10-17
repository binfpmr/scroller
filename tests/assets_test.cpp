// Every file under assets/ must load: sprite sheets parse, levels parse and their goals are reachable.
#include <filesystem>
#include <iostream>

#include "game_test_util.h"
#include "reachability.h"

namespace fs = std::filesystem;

TEST(AllSpriteSheetsParse) {
  int count = 0;
  for (const auto& entry : fs::directory_iterator(fs::path(SCROLLER_ASSETS_DIR) / "sprites")) {
    const std::string name = "sprites/" + entry.path().filename().string();
    const std::string text = test::ReadAsset(name.c_str());
    std::vector<uint8_t> storage(1 << 20);
    lib::Arena arena(storage.data(), storage.size());
    gfx::SpriteSheet sheet;
    const char* error = nullptr;
    int line = 0;
    if (!sheet.Parse(text.data(), text.size(), arena, &error, &line)) {
      std::cerr << name << " line " << line << ": " << error << "\n";
      test::Fail(__FILE__, __LINE__, "sprite sheet does not parse");
    }
    ++count;
  }
  EXPECT_TRUE(count >= 2);
}

TEST(AllLevelsLoadAndGoalsAreReachable) {
  const test::TestAssets assets;
  int count = 0;
  for (const auto& entry : fs::directory_iterator(fs::path(SCROLLER_ASSETS_DIR) / "levels")) {
    const std::string name = "levels/" + entry.path().filename().string();
    const std::string text = test::ReadAsset(name.c_str());
    std::vector<uint8_t> storage(1 << 20);
    lib::Arena arena(storage.data(), storage.size());
    game::Game g;
    const char* error = nullptr;
    if (!g.Init(arena, text.data(), text.size(), assets.assets(), &error)) {
      std::cerr << name << ": " << error << "\n";
      test::Fail(__FILE__, __LINE__, "level does not load");
      continue;
    }
    bool has_goal = false;
    for (int i = 0; i < g.level().width * g.level().height; ++i) {
      has_goal = has_goal || g.level().tiles[i] == game::Tile::kGoal;
    }
    if (has_goal && !levelcheck::Explore(g.level()).ReachesGoal(g.level())) {
      std::cerr << name << ": goal not reachable (run levelcheck)\n";
      test::Fail(__FILE__, __LINE__, "goal not reachable");
    }
    ++count;
  }
  EXPECT_TRUE(count >= 1);
}
