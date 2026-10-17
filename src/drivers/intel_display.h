// Vertical sync on Intel integrated graphics (Gen4 and later, checked against Gen9 / UHD 630) through the display
// pipe's scanline counter PIPEDSL in the GPU's MMIO BAR. No mode setting: the firmware's display setup is reused.
#pragma once

#include <stdint.h>

#include "drivers/pci.h"

namespace intel_display {

class Scanline {
 public:
  // Maps nothing new: needs BAR0 to be identity-mapped by the firmware. Finds the enabled pipe and checks that its
  // scanline counter wraps at a plausible refresh rate. False if any of that fails.
  bool Init(const pci::Device& gpu, uint64_t tsc_hz);

  // Waits until the counter wraps (a new frame starts at the top), at most `timeout_cycles`.
  void WaitForFrameStart(uint64_t timeout_cycles) const;

  char pipe_name() const { return static_cast<char>('A' + pipe_); }
  uint64_t period_cycles() const { return period_cycles_; }

 private:
  uint32_t Line() const;

  volatile const uint32_t* mmio_ = nullptr;
  int pipe_ = 0;
  uint64_t period_cycles_ = 0;
};

}  // namespace intel_display
