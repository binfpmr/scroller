// Every file under assets/ must load: sprite sheets parse, levels parse, their goals are reachable and they have no
// dead ends (spots the player can get to but never leave toward the goal).
#include <filesystem>
#include <iostream>

#include "audio/song.h"
#include "game_test_util.h"

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

// Reachability (goal reachable, no dead ends) is checked per level by levelcheck itself: one CTest entry per level
// file (tests/CMakeLists.txt), so they run in parallel.
TEST(AllLevelsLoad) {
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
    }
    ++count;
  }
  EXPECT_TRUE(count >= 1);
}

TEST(AllSongsParse) {
  int count = 0;
  for (const char* dir : {"music", "sounds"}) {
    for (const auto& entry : fs::directory_iterator(fs::path(SCROLLER_ASSETS_DIR) / dir)) {
      const std::string name = std::string(dir) + "/" + entry.path().filename().string();
      const std::string text = test::ReadAsset(name.c_str());
      std::vector<uint8_t> storage(1 << 20);
      lib::Arena arena(storage.data(), storage.size());
      audio::Song song;
      const char* error = nullptr;
      int line = 0;
      if (!song.Parse(text.data(), text.size(), arena, &error, &line)) {
        std::cerr << name << " line " << line << ": " << error << "\n";
        test::Fail(__FILE__, __LINE__, "song does not parse");
      }
      ++count;
    }
  }
  EXPECT_TRUE(count >= 5);
}
