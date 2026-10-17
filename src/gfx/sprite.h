// Sprites parsed from plain-text sprite sheets, and drawing them.
//
// Sheet format (one directive per line; lines starting with '#' are comments, except inside a frame):
//   palette            followed by lines "<char> <rrggbb>", one per colour; '.' is always transparent
//   size <w> <h>       size of the frames that follow
//   frame <name>       followed by exactly <h> rows of exactly <w> palette characters
#pragma once

#include <stddef.h>

#include "gfx/surface.h"
#include "lib/arena.h"

namespace gfx {

// Pixels with this bit set are not drawn.
inline constexpr Color kTransparent = 0xff000000u;

struct Sprite {
  int width = 0;
  int height = 0;
  const Color* pixels = nullptr;  // row-major, width * height
};

class SpriteSheet {
 public:
  static constexpr int kMaxFrames = 48;
  static constexpr int kMaxNameLength = 23;

  // Parses `text` with pixel storage from `arena`. On error returns false; *error is a static message and *error_line
  // the 1-based line it refers to.
  bool Parse(const char* text, size_t length, lib::Arena& arena, const char** error, int* error_line);

  // Null if there is no frame with that name.
  const Sprite* Find(const char* name) const;
  int frame_count() const { return count_; }

 private:
  struct Frame {
    char name[kMaxNameLength + 1];
    Sprite sprite;
  };
  Frame frames_[kMaxFrames];
  int count_ = 0;
};

// Draws `sprite` with its top-left corner at (x, y), mirrored horizontally if `flip_x`, clipped to the surface.
void DrawSprite(Surface& s, const Sprite& sprite, int x, int y, bool flip_x = false);

}  // namespace gfx
