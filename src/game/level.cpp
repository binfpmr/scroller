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
    if (c != '#' && c != 'X' && c != 'G' && c != 'C' && c != '.' && c != ' ' && c != 'P') {
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
    }
    ++x;
  }
  level->width = width;
  level->height = height;
  level->tiles = tiles;
  return true;
}

}  // namespace game
