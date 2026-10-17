// Built-in 5x7 bitmap font for debug text and simple HUDs: digits, upper case letters (lower case is drawn as upper
// case) and a few symbols; anything else is drawn as '?'.
#pragma once

#include "gfx/surface.h"

namespace gfx {

inline constexpr int kGlyphWidth = 5;
inline constexpr int kGlyphHeight = 7;
inline constexpr int kCharAdvance = 6;  // glyph plus one column of spacing
inline constexpr int kLineHeight = 9;

// Draws `text` with its top-left corner at (x, y), clipped to the surface; '\n' starts a new line.
void DrawText(Surface& s, int x, int y, const char* text, Color color);

// Width in pixels of the longest line of `text`.
int TextWidth(const char* text);

}  // namespace gfx
