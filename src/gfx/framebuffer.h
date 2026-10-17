// Description of the hardware framebuffer (from UEFI GOP).
#pragma once

#include <stdint.h>

namespace gfx {

// Byte order of a 32-bit pixel in memory.
enum class PixelOrder : uint8_t {
  kBgrx,  // same layout as gfx::Color (0x00RRGGBB little-endian): copy as is
  kRgbx,  // red and blue swapped
};

struct Framebuffer {
  uint32_t* base = nullptr;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;  // pixels per scan line
  uint64_t size = 0;    // bytes of framebuffer memory (may exceed stride * height * 4)
  PixelOrder order = PixelOrder::kBgrx;
};

}  // namespace gfx
