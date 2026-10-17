#include "game/scroll_demo.h"

#include "lib/mem.h"

namespace game {

using gfx::Color;
using gfx::Rgb;
using platform::Key;

void ScrollDemo::Update(const platform::InputState& input) {
  const int dx = (input.Held(Key::kRight) ? kSpeed : 0) - (input.Held(Key::kLeft) ? kSpeed : 0);
  const int dy = (input.Held(Key::kDown) ? kSpeed : 0) - (input.Held(Key::kUp) ? kSpeed : 0);
  camera_x_ = lib::Clamp(camera_x_ + dx, 0, kWorldWidth - gfx::kScreenWidth);
  camera_y_ = lib::Clamp(camera_y_ + dy, 0, kWorldHeight - gfx::kScreenHeight);
}

void ScrollDemo::Render(gfx::Surface& screen) const {
  for (int y = 0; y < screen.height; ++y) {
    Color* row = screen.Row(y);
    const int wy = camera_y_ + y;
    for (int x = 0; x < screen.width; ++x) {
      const int wx = camera_x_ + x;
      const int tx = wx >> 4;
      const int ty = wy >> 4;
      Color c;
      if ((wx & 127) == 0 || (wy & 127) == 0) {
        c = Rgb(255, 255, 255);  // coarse grid every 128 px
      } else if ((wx & 15) == 0 || (wy & 15) == 0) {
        c = Rgb(20, 20, 30);  // tile edges
      } else {
        // Colour drifts across the world so the position is visible; checkerboard for motion.
        const uint8_t shade = ((tx + ty) & 1) ? 40 : 0;
        c = Rgb(static_cast<uint8_t>(tx * 255 / (kWorldWidth / 16)), static_cast<uint8_t>(60 + shade),
                static_cast<uint8_t>(ty * 255 / (kWorldHeight / 16)));
      }
      row[x] = c;
    }
  }

  // Fixed marker in the centre of the screen.
  for (int y = gfx::kScreenHeight / 2 - 8; y < gfx::kScreenHeight / 2 + 8; ++y) {
    for (int x = gfx::kScreenWidth / 2 - 8; x < gfx::kScreenWidth / 2 + 8; ++x) {
      screen.Row(y)[x] = Rgb(255, 220, 0);
    }
  }
}

}  // namespace game
