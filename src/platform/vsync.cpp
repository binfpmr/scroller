#include "platform/vsync.h"

#include "drivers/vga_retrace.h"
#include "lib/format.h"

namespace platform {

void Vsync::Init(const pci::Device* gpu, uint64_t tsc_hz) {
  if (gpu != nullptr && intel_.Init(*gpu, tsc_hz)) {
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

void Vsync::WaitForFrameStart() const {
  switch (method_) {
    case Method::kIntelScanline:
      intel_.WaitForFrameStart(2 * period_cycles_);
      break;
    case Method::kVgaRetrace:
      vga::WaitForRetraceStart(2 * period_cycles_);
      break;
    case Method::kNone:
      break;
  }
}

}  // namespace platform
