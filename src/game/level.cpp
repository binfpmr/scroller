#include "game/level.h"

namespace game {

bool ParseLevel(const char* text, size_t length, lib::Arena& arena, Level* level, const char** error) {
  // Pass 1: size and validation.
  int width = 0;
  int height = 0;
  int line_width = 0;
  int starts = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (c == '\n') {
      ++height;
      line_width = 0;
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (c != '#' && c != 'X' && c != 'G' && c != 'C' && c != '.' && c != ' ' && c != 'P' && c != 'w' && c != 'j' &&
        c != 'f') {
      *error = "unknown tile character";
      return false;
    }
    starts += c == 'P' ? 1 : 0;
    ++line_width;
    width = line_width > width ? line_width : width;
  }
  if (line_width > 0) {
    ++height;  // last line without a newline
  }
  if (width == 0 || height == 0) {
    *error = "empty level";
    return false;
  }
  if (starts != 1) {
    *error = "level needs exactly one player start 'P'";
    return false;
  }

  Tile* tiles = arena.AllocateArray<Tile>(static_cast<size_t>(width) * height);
  if (tiles == nullptr) {
    *error = "out of memory for the level";
    return false;
  }
  for (int i = 0; i < width * height; ++i) {
    tiles[i] = Tile::kEmpty;
  }
  EnemySpawn* enemies = arena.AllocateArray<EnemySpawn>(Level::kMaxEnemies);
  int enemy_count = 0;
  if (enemies == nullptr) {
    *error = "out of memory for the level";
    return false;
  }

  // Pass 2: fill.
  int x = 0;
  int y = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (c == '\n') {
      ++y;
      x = 0;
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (c == '#') {
      tiles[y * width + x] = Tile::kGround;
    } else if (c == 'X') {
      tiles[y * width + x] = Tile::kStone;
    } else if (c == 'G') {
      tiles[y * width + x] = Tile::kGoal;
    } else if (c == 'C') {
      tiles[y * width + x] = Tile::kCheckpoint;
    } else if (c == 'P') {
      level->start_x = x;
      level->start_y = y;
    } else if (c == 'w' || c == 'j' || c == 'f') {
      if (enemy_count == Level::kMaxEnemies) {
        *error = "too many enemies";
        return false;
      }
      enemies[enemy_count++] = {.kind = c == 'w'   ? EnemyKind::kWalker
                                        : c == 'j' ? EnemyKind::kJumper
                                                   : EnemyKind::kFlyer,
                                .tx = x,
                                .ty = y};
    }
    ++x;
  }
  level->width = width;
  level->height = height;
  level->tiles = tiles;
  level->enemies = enemies;
  level->enemy_count = enemy_count;
  level->ground_rows = 0;
  for (int row = height - 1; row >= 0; --row) {
    int solid = 0;
    for (int col = 0; col < width; ++col) {
      solid += level->IsSolid(col, row) ? 1 : 0;
    }
    if (solid * 2 < width) {
      break;
    }
    ++level->ground_rows;
  }
  return true;
}

}  // namespace game
