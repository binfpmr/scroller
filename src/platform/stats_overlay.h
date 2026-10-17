// Performance numbers drawn over the game (hidden until F1): frames per second, render and present time, actual CPU
// clock (APERF/MPERF), TSC clock and the framebuffer write-combining state. Meant for real hardware, where there is no
// log; the same text goes to the debug log every 5 seconds.
#pragma once

#include <stdint.h>

#include "gfx/surface.h"

namespace platform {

class StatsOverlay {
 public:
  explicit StatsOverlay(uint64_t tsc_hz);

  void AddFrame(uint64_t render_cycles, uint64_t present_cycles);
  // Recomputes the text once per second; `wc` is "ON" or "OFF" (framebuffer write-combining).
  void Update(uint64_t now, const char* wc);
  void Draw(gfx::Surface& screen) const;
  // Fixed lines shown under the numbers (hardware description), copied.
  void SetInfo(const char* info);

  bool visible = false;  // F1

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
  char info_[240] = "";
};

}  // namespace platform
