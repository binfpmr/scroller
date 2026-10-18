// The game: owns the level, player and camera; updated at a fixed 60 Hz.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "game/camera.h"
#include "game/level.h"
#include "game/player.h"
#include "gfx/sprite.h"
#include "gfx/surface.h"
#include "lib/arena.h"
#include "platform/input.h"

namespace game {

// Sprite sheets the game draws with (parsed from the asset pack by the caller).
struct GameAssets {
  const gfx::SpriteSheet* player = nullptr;  // sprites/player.txt
  const gfx::SpriteSheet* tiles = nullptr;   // sprites/tiles.txt
};

// One level's text (levels/levelN.txt).
struct LevelText {
  const char* data = nullptr;
  size_t size = 0;
};

// Sprite frames resolved once at Init.
struct Frames {
  const gfx::Sprite* idle;
  const gfx::Sprite* blink;
  const gfx::Sprite* run[4];
  const gfx::Sprite* jump;
  const gfx::Sprite* fall;
  const gfx::Sprite* duck;
  const gfx::Sprite* ground_top;
  const gfx::Sprite* ground;
  const gfx::Sprite* stone;
  const gfx::Sprite* flag;
};

class Game {
 public:
  static constexpr int kWinTicks = 180;    // "well done" shown this long before the next level starts
  static constexpr int kTitleTicks = 120;  // "level N" shown this long when a level starts
  static constexpr int kMaxLevels = 8;

  // Parses the levels into memory from `arena` and looks up all sprite frames. The game starts with the first level;
  // reaching a goal moves on to the next, after the last back to the first. On error returns false with a message in
  // *error (static storage, valid until the next call).
  bool Init(lib::Arena& arena, const LevelText* levels, int level_count, const GameAssets& assets, const char** error);
  // A single level.
  bool Init(lib::Arena& arena, const char* level_text, size_t level_length, const GameAssets& assets,
            const char** error) {
    const LevelText level = {level_text, level_length};
    return Init(arena, &level, 1, assets, error);
  }
  void Update(const platform::InputState& input);
  void Render(gfx::Surface& screen) const;

  const Level& level() const { return levels_[current_]; }
  int level_index() const { return current_; }  // 0-based
  int level_count() const { return level_count_; }
  const Player& player() const { return player_; }
  const Camera& camera() const { return camera_; }
  bool won() const { return win_ticks_ > 0; }

  // Sound events since the last call (PlayerEvent bits, plus kEventGoal), then cleared.
  static constexpr uint8_t kEventGoal = 1 << 7;
  uint8_t TakeEvents() {
    const uint8_t e = events_;
    events_ = 0;
    return e;
  }
  uint32_t ticks() const { return ticks_; }

 private:
  void Restart();  // the player back at the current level's start
  bool TouchesGoal() const;

  Level levels_[kMaxLevels];
  int level_count_ = 0;
  int current_ = 0;
  int title_ticks_ = 0;
  Player player_;
  Camera camera_;
  Frames frames_{};
  uint32_t ticks_ = 0;
  int win_ticks_ = 0;
  uint8_t events_ = 0;
};

}  // namespace game
