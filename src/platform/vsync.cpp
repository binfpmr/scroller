#include "platform/vsync.h"

#include "arch/x86_64/cpu.h"
#include "drivers/vga_retrace.h"
#include "lib/format.h"

namespace platform {

void Vsync::Init(const pci::Device* gpu, uint64_t tsc_hz, lib::Arena* arena) {
  if (gpu != nullptr && gpu->vendor_id == 0x8086 && intel_.Init(*gpu, tsc_hz, arena)) {
    method_ = Method::kIntelScanline;
    period_cycles_ = intel_.period_cycles();
  } else if (vga::ProbeRetrace(tsc_hz, &period_cycles_)) {
    method_ = Method::kVgaRetrace;
  }
  if (method_ == Method::kNone) {
    return;
  }
  const uint64_t hz10 = tsc_hz * 10 / period_cycles_;
  char method[16];
  if (method_ == Method::kIntelScanline) {
    lib::Format(method, sizeof(method), "INTEL PIPE %c", intel_.pipe_name());
  } else {
    lib::Format(method, sizeof(method), "VGA 3DA");
  }
  lib::Format(description_, sizeof(description_), "%s %llu.%llu HZ", method, static_cast<unsigned long long>(hz10 / 10),
              static_cast<unsigned long long>(hz10 % 10));
}

void Vsync::WaitForFrameStart(void (*poll)(void* context), void* context) const {
  if (method_ == Method::kNone) {
    return;
  }
  const uint64_t deadline = x86::Rdtsc() + 2 * period_cycles_;
  if (method_ == Method::kIntelScanline) {
    // A new frame starts when the scanline counter wraps.
    uint32_t last = intel_.Line();
    while (x86::Rdtsc() < deadline) {
      poll(context);
      const uint32_t line = intel_.Line();
      if (line < last) {
        return;
      }
      last = line;
    }
    return;
  }
  // VGA: wait for the end of a retrace in progress, then for the next one to begin.
  while (vga::InRetrace() && x86::Rdtsc() < deadline) {
    poll(context);
  }
  while (!vga::InRetrace() && x86::Rdtsc() < deadline) {
    poll(context);
  }
}

}  // namespace platform
