// The game's drawing target: 32-bit pixels in RAM, scaled to the real framebuffer by gfx::Present.
#pragma once

#include <stdint.h>

namespace gfx {

// 0x00RRGGBB.
using Color = uint32_t;

constexpr Color Rgb(uint8_t r, uint8_t g, uint8_t b) { return (Color{r} << 16) | (Color{g} << 8) | b; }

inline constexpr int kScreenWidth = 320;
inline constexpr int kScreenHeight = 240;

struct Surface {
  Color* pixels = nullptr;
  int width = 0;
  int height = 0;  // rows are `width` pixels apart

  Color* Row(int y) { return pixels + y * width; }
  const Color* Row(int y) const { return pixels + y * width; }
};

}  // namespace gfx
