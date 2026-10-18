// Performance numbers drawn over the game (hidden until F1): frames per second, render and present time, actual CPU
// clock (APERF/MPERF), TSC clock and the framebuffer write-combining state. Meant for real hardware, where there is no
// log; the same text goes to the debug log every 5 seconds.
#pragma once

#include <stdint.h>

#include "gfx/surface.h"

namespace platform {

// Live state shown on the overlay; counters are running totals (rates are computed per update).
struct LiveStats {
  const char* write_combining = "OFF";
  uint64_t timer_ticks = 0;
  uint64_t unexpected_irqs = 0;
  const char* keyboard = "";
  const char* sound = "NONE";
  uint64_t sound_irqs = 0;
  uint16_t last_key = 0;
  const char* network = "NONE";
  const char* network_detail = "";
  const char* usb = "NONE";
  uint64_t frames_received = 0;
  uint64_t frames_sent = 0;
};

class StatsOverlay {
 public:
  explicit StatsOverlay(uint64_t tsc_hz);

  // Per frame: drawing into the 320x240 surface, waiting for vsync, copying to the framebuffer.
  void AddFrame(uint64_t render_cycles, uint64_t wait_cycles, uint64_t present_cycles);
  // Recomputes the text once per second.
  void Update(uint64_t now, const LiveStats& live);
  void Draw(gfx::Surface& screen) const;
  // Fixed lines shown under the numbers (hardware description), copied.
  void SetInfo(const char* info);

  bool visible = false;  // F1

 private:
  uint64_t tsc_hz_;
  uint64_t period_start_;
  uint64_t frames_ = 0;
  uint64_t render_cycles_ = 0;
  uint64_t wait_cycles_ = 0;
  uint64_t present_cycles_ = 0;
  bool has_aperf_mperf_;
  uint64_t last_aperf_ = 0;
  uint64_t last_mperf_ = 0;
  int updates_ = 0;
  uint64_t last_timer_ticks_ = 0;
  uint64_t last_sound_irqs_ = 0;
  char text_[480] = "";
  char info_[400] = "";
};

}  // namespace platform
