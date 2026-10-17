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
constexpr uint32_t kPipeEnable = 1u << 31;

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

  uint32_t conf[3];
  pipe_ = -1;
  for (int pipe = 0; pipe < 3; ++pipe) {
    conf[pipe] = Read(kPipeConf + pipe * kPipeStride);
    if (pipe_ < 0 && (conf[pipe] & kPipeEnable) != 0) {
      pipe_ = pipe;
    }
  }
  const int sample_pipe = pipe_ < 0 ? 0 : pipe_;

  // Time a few wraps of the scanline counter.
  constexpr int kWraps = 4;
  uint64_t wraps[kWraps];
  int count = 0;
  const uint64_t deadline = x86::Rdtsc() + tsc_hz / 8;
  const int saved_pipe = pipe_;
  pipe_ = sample_pipe;
  uint32_t last = Line();
  uint32_t max_line = 0;
  while (count < kWraps && x86::Rdtsc() < deadline) {
    const uint32_t line = Line();
    if (line < last) {
      wraps[count++] = x86::Rdtsc();
    }
    max_line = line > max_line ? line : max_line;
    last = line;
  }
  pipe_ = saved_pipe;
  const uint64_t period = count >= 2 ? (wraps[count - 1] - wraps[0]) / (count - 1) : 0;
  const uint64_t hz10 = period == 0 ? 0 : tsc_hz * 10 / period;
  lib::Format(status_, sizeof(status_), "BAR0 %llx%s\nPIPECONF %x/%x/%x MAXLINE %u %llu.%llu HZ",
              static_cast<unsigned long long>(bar), mapped_by_us ? " (MAPPED)" : "", conf[0], conf[1], conf[2],
              max_line, static_cast<unsigned long long>(hz10 / 10), static_cast<unsigned long long>(hz10 % 10));
  Log("intel: %s\n", status_);
  if (pipe_ < 0 || count < 3 || hz10 < 300 || hz10 > 1250) {
    return false;
  }
  period_cycles_ = period;
  return true;
}

void Scanline::WaitForFrameStart(uint64_t timeout_cycles) const {
  const uint64_t deadline = x86::Rdtsc() + timeout_cycles;
  uint32_t last = Line();
  while (x86::Rdtsc() < deadline) {
    const uint32_t line = Line();
    if (line < last) {
      return;
    }
    last = line;
    x86::Pause();
  }
}

}  // namespace intel_display
