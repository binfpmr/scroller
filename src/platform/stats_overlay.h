// Performance numbers drawn over the game (F1 toggles): frames per second, render and present time, actual CPU clock
// (APERF/MPERF), TSC clock and the framebuffer write-combining state. Meant for real hardware, where there is no log.
#pragma once

#include <stdint.h>

#include "gfx/surface.h"

namespace platform {

class StatsOverlay {
 public:
  explicit StatsOverlay(uint64_t tsc_hz);

  void AddFrame(uint64_t render_cycles, uint64_t present_cycles);
  // Recomputes the text once per second; `wc` is "ON", "OFF" or "N/A".
  void Update(uint64_t now, const char* wc);
  void Draw(gfx::Surface& screen) const;

  bool visible = true;

 private:
  uint64_t tsc_hz_;
  uint64_t period_start_;
  uint64_t frames_ = 0;
  uint64_t render_cycles_ = 0;
  uint64_t present_cycles_ = 0;
  bool has_aperf_mperf_;
  uint64_t last_aperf_ = 0;
  uint64_t last_mperf_ = 0;
  int updates_ = 0;
  char text_[192] = "";
};

}  // namespace platform
