// Waiting for the display's vertical blank before presenting, to avoid tearing. GOP has no such service, so this
// tries what the hardware offers: the Intel display pipe's scanline counter, then the legacy VGA retrace bit.
#pragma once

#include <stdint.h>

#include "drivers/intel_display.h"
#include "drivers/pci.h"

namespace platform {

class Vsync {
 public:
  // `gpu` may be null (no display controller found).
  void Init(const pci::Device* gpu, uint64_t tsc_hz);
  bool active() const { return method_ != Method::kNone; }
  // Short description for the stats overlay, e.g. "INTEL PIPE A 60.0 HZ" or "NONE".
  const char* description() const { return description_; }
  void WaitForFrameStart() const;

 private:
  enum class Method { kNone, kIntelScanline, kVgaRetrace };

  Method method_ = Method::kNone;
  intel_display::Scanline intel_;
  uint64_t period_cycles_ = 0;
  char description_[32] = "NONE";
};

}  // namespace platform
