// Vertical sync on Intel integrated graphics (Gen4 and later, checked against Gen9 / UHD 630) through the display
// pipe's scanline counter PIPEDSL in the GPU's MMIO BAR. No mode setting: the firmware's display setup is reused.
#pragma once

#include <stdint.h>

#include "drivers/pci.h"
#include "lib/arena.h"

namespace intel_display {

class Scanline {
 public:
  // Finds BAR0 (mapping it uncached if the firmware did not and `arena` is given, i.e. we own the page tables) and the
  // first pipe whose scanline counter wraps at a plausible refresh rate. On failure status() says why.
  bool Init(const pci::Device& gpu, uint64_t tsc_hz, lib::Arena* arena);

  // Waits until the counter wraps (a new frame starts at the top), at most `timeout_cycles`.
  void WaitForFrameStart(uint64_t timeout_cycles) const;

  char pipe_name() const { return static_cast<char>('A' + pipe_); }
  uint64_t period_cycles() const { return period_cycles_; }
  // What Init found, for the overlay (two lines), e.g. "BAR0 B2000000 PIPE A\n1140 LINES 60.0 HZ  EDP CONF C0000000".
  const char* status() const { return status_; }

 private:
  uint32_t Line() const;
  // Refresh period of pipe_ in TSC cycles from a few counter wraps (also updates max_line_); 0 if not scanning.
  uint64_t MeasurePeriod(uint64_t tsc_hz);
  uint32_t Read(uint32_t offset) const { return mmio_[offset / 4]; }

  volatile const uint32_t* mmio_ = nullptr;
  int pipe_ = 0;
  uint64_t period_cycles_ = 0;
  uint32_t max_line_ = 0;
  char status_[96] = "";
};

}  // namespace intel_display
