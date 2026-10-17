// The game: owns the level, player and camera; updated at a fixed 60 Hz.
#pragma once

#include <stddef.h>

#include "game/camera.h"
#include "game/level.h"
#include "game/player.h"
#include "gfx/surface.h"
#include "lib/arena.h"
#include "platform/input.h"

namespace game {

class Game {
 public:
  // Parses the level text into memory from `arena`. On error returns false with a static message in *error.
  bool Init(lib::Arena& arena, const char* level_text, size_t level_length, const char** error);
  void Update(const platform::InputState& input);
  void Render(gfx::Surface& screen) const;

  const Level& level() const { return level_; }
  const Player& player() const { return player_; }
  const Camera& camera() const { return camera_; }

 private:
  Level level_;
  Player player_;
  Camera camera_;
};

}  // namespace game
