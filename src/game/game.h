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
  static constexpr int kWinTicks = 180;  // "well done" shown this long before the level restarts

  // Parses the level text into memory from `arena` and looks up all sprite frames. On error returns false with a
  // message in *error (static storage, valid until the next call).
  bool Init(lib::Arena& arena, const char* level_text, size_t level_length, const GameAssets& assets,
            const char** error);
  void Update(const platform::InputState& input);
  void Render(gfx::Surface& screen) const;

  const Level& level() const { return level_; }
  const Player& player() const { return player_; }
  const Camera& camera() const { return camera_; }
  bool won() const { return win_ticks_ > 0; }
  uint32_t ticks() const { return ticks_; }

 private:
  void Restart();
  bool TouchesGoal() const;

  Level level_;
  Player player_;
  Camera camera_;
  Frames frames_{};
  uint32_t ticks_ = 0;
  int win_ticks_ = 0;
};

}  // namespace game
