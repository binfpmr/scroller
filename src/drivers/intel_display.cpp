#include "drivers/intel_display.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "lib/format.h"

namespace intel_display {
namespace {

// Register offsets in BAR0 (Intel display, "pipe" registers; pipe B and C at +0x1000 / +0x2000).
constexpr uint32_t kPipeDsl = 0x70000;   // display scan line, bits 12:0
constexpr uint32_t kPipeConf = 0x70008;  // bit 31: pipe enabled
constexpr uint32_t kPipeStride = 0x1000;
constexpr uint32_t kDslMask = 0x1fff;
constexpr uint32_t kTransConfEdp = 0x7f008;  // eDP transcoder configuration (Gen7.5+), diagnostics only

}  // namespace

uint32_t Scanline::Line() const { return mmio_[(kPipeDsl + pipe_ * kPipeStride) / 4] & kDslMask; }

bool Scanline::Init(const pci::Device& gpu, uint64_t tsc_hz, lib::Arena* arena) {
  if (gpu.vendor_id != 0x8086) {
    lib::Format(status_, sizeof(status_), "NOT AN INTEL GPU");
    return false;
  }
  const uint32_t command = pci::Read32(gpu.bus, gpu.device, gpu.function, 0x04) & 0xffff;
  const uint32_t bar_low = pci::Read32(gpu.bus, gpu.device, gpu.function, 0x10);
  uint64_t bar = bar_low & ~0xfull;
  if (((bar_low >> 1) & 3) == 2) {  // 64-bit BAR
    bar |= static_cast<uint64_t>(pci::Read32(gpu.bus, gpu.device, gpu.function, 0x14)) << 32;
  }
  if ((bar_low & 1) != 0 || bar == 0 || (command & 0x2) == 0) {
    lib::Format(status_, sizeof(status_), "BAR0 %llx CMD %x: NO MMIO", static_cast<unsigned long long>(bar_low),
                command);
    return false;
  }
  constexpr uint64_t kMmioSize = 1 << 20;  // display registers live in the first MiB of the 16 MiB BAR
  bool mapped_by_us = false;
  if (!x86::IsMapped(bar + kPipeDsl) || !x86::IsMapped(bar + kPipeDsl + 2 * kPipeStride)) {
    if (arena == nullptr || !x86::MapUncached(*arena, bar, kMmioSize) || !x86::IsMapped(bar + kPipeDsl) ||
        !x86::IsMapped(bar + kPipeDsl + 2 * kPipeStride)) {
      lib::Format(status_, sizeof(status_), "BAR0 %llx NOT MAPPED", static_cast<unsigned long long>(bar));
      return false;
    }
    mapped_by_us = true;
  }
  mmio_ = reinterpret_cast<volatile const uint32_t*>(bar);

  // Which pipe drives the panel: PIPECONF is not reliable for that (on Gen9 an eDP panel runs through the eDP
  // transcoder, TRANSCONF_EDP, and PIPECONF A reads 0 although pipe A scans out). So take the first pipe whose
  // scanline counter actually wraps at a plausible refresh rate.
  for (int pipe = 0; pipe < 3; ++pipe) {
    pipe_ = pipe;
    max_line_ = 0;
    const uint64_t period = MeasurePeriod(tsc_hz);
    const uint64_t hz10 = period == 0 ? 0 : tsc_hz * 10 / period;
    if (hz10 >= 300 && hz10 <= 1250) {
      period_cycles_ = period;
      lib::Format(status_, sizeof(status_), "BAR0 %llx%s PIPE %c\n%u LINES %llu.%llu HZ  EDP CONF %x",
                  static_cast<unsigned long long>(bar), mapped_by_us ? " (MAPPED)" : "", pipe_name(), max_line_ + 1,
                  static_cast<unsigned long long>(hz10 / 10), static_cast<unsigned long long>(hz10 % 10),
                  Read(kTransConfEdp));
      Log("intel: %s\n", status_);
      return true;
    }
  }
  lib::Format(status_, sizeof(status_), "BAR0 %llx%s\nNO PIPE SCANNING  CONF %x/%x/%x EDP %x",
              static_cast<unsigned long long>(bar), mapped_by_us ? " (MAPPED)" : "", Read(kPipeConf),
              Read(kPipeConf + kPipeStride), Read(kPipeConf + 2 * kPipeStride), Read(kTransConfEdp));
  Log("intel: %s\n", status_);
  return false;
}

uint64_t Scanline::MeasurePeriod(uint64_t tsc_hz) {
  // A counter that does not move within ~1 ms belongs to an idle pipe.
  const uint32_t first = Line();
  const uint64_t quick = x86::Rdtsc() + tsc_hz / 1000;
  while (Line() == first) {
    if (x86::Rdtsc() > quick) {
      return 0;
    }
  }
  // Time a few wraps.
  constexpr int kWraps = 4;
  uint64_t wraps[kWraps];
  int count = 0;
  const uint64_t deadline = x86::Rdtsc() + tsc_hz / 8;
  uint32_t last = Line();
  while (count < kWraps && x86::Rdtsc() < deadline) {
    const uint32_t line = Line();
    if (line < last) {
      wraps[count++] = x86::Rdtsc();
    }
    max_line_ = line > max_line_ ? line : max_line_;
    last = line;
  }
  return count >= 3 ? (wraps[count - 1] - wraps[0]) / (count - 1) : 0;
}

}  // namespace intel_display
