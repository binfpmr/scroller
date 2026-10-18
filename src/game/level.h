// Tile map of a level, parsed from plain text (one character per tile).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lib/arena.h"

namespace game {

inline constexpr int kTileSize = 16;
inline constexpr int kTileShift = 4;

enum class Tile : uint8_t {
  kEmpty,
  kGround,      // '#': dirt, drawn with grass when open above
  kStone,       // 'X'
  kGoal,        // 'G': flag, not solid; touching it finishes the level
  kCheckpoint,  // 'C': not solid; touching it makes it the respawn point
};

enum class EnemyKind : uint8_t { kWalker, kJumper, kFlyer };

struct EnemySpawn {  // trivially constructible: allocated with Arena::AllocateArray
  EnemyKind kind;
  int tx;
  int ty;
};

struct Level {
  static constexpr int kMaxEnemies = 64;

  int width = 0;  // in tiles
  int height = 0;
  Tile* tiles = nullptr;  // row-major
  int start_x = 0;        // player start tile
  int start_y = 0;
  EnemySpawn* enemies = nullptr;  // where enemies start (their tiles are empty)
  int enemy_count = 0;
  int ground_rows = 0;  // rows at the bottom that are mostly solid: the ground the background's bushes stand on

  // Outside the map: solid to the left and right (walls), empty above and below (below: a bottomless pit).
  Tile At(int tx, int ty) const {
    if (tx < 0 || tx >= width) {
      return Tile::kStone;
    }
    if (ty < 0 || ty >= height) {
      return Tile::kEmpty;
    }
    return tiles[ty * width + tx];
  }
  bool IsSolid(int tx, int ty) const {
    const Tile t = At(tx, ty);
    return t == Tile::kGround || t == Tile::kStone;
  }
  int pixel_width() const { return width * kTileSize; }
  int pixel_height() const { return height * kTileSize; }
};

// Text format, one line per tile row ('\n' or "\r\n"); shorter lines are padded with empty tiles:
//   '#' ground (solid)   'X' stone (solid)   'G' goal flag   'C' checkpoint   '.' or ' ' empty
//   'P' player start (exactly one)   'w' walker   'j' jumper   'f' flyer (at most kMaxEnemies)
// On error returns false and points *error at a static message.
bool ParseLevel(const char* text, size_t length, lib::Arena& arena, Level* level, const char** error);

}  // namespace game
