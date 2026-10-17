// M1 stand-in for the game: a patterned world larger than the screen, scrolled in four directions with the arrows.
#pragma once

#include "gfx/surface.h"
#include "platform/input.h"

namespace game {

class ScrollDemo {
 public:
  static constexpr int kWorldWidth = 1024;
  static constexpr int kWorldHeight = 768;
  static constexpr int kSpeed = 2;  // pixels per tick

  void Update(const platform::InputState& input);
  void Render(gfx::Surface& screen) const;

  int camera_x() const { return camera_x_; }
  int camera_y() const { return camera_y_; }

 private:
  int camera_x_ = 0;
  int camera_y_ = 0;
};

}  // namespace game
