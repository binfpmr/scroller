#include "drivers/intel_display.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"

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

bool Scanline::Init(const pci::Device& gpu, uint64_t tsc_hz) {
  if (gpu.vendor_id != 0x8086) {
    return false;
  }
  const uint32_t bar_low = pci::Read32(gpu.bus, gpu.device, gpu.function, 0x10);
  if ((bar_low & 1) != 0) {
    Log("intel: BAR0 is not a memory BAR\n");
    return false;
  }
  uint64_t bar = bar_low & ~0xfull;
  if (((bar_low >> 1) & 3) == 2) {  // 64-bit BAR
    bar |= static_cast<uint64_t>(pci::Read32(gpu.bus, gpu.device, gpu.function, 0x14)) << 32;
  }
  if (bar == 0 || !x86::IsMapped(bar + kPipeDsl) || !x86::IsMapped(bar + kPipeDsl + 2 * kPipeStride)) {
    Log("intel: BAR0 %llx not mapped\n", static_cast<unsigned long long>(bar));
    return false;
  }
  mmio_ = reinterpret_cast<volatile const uint32_t*>(bar);

  pipe_ = -1;
  for (int pipe = 0; pipe < 3; ++pipe) {
    if ((mmio_[(kPipeConf + pipe * kPipeStride) / 4] & kPipeEnable) != 0) {
      pipe_ = pipe;
      break;
    }
  }
  if (pipe_ < 0) {
    Log("intel: no enabled pipe\n");
    return false;
  }

  // Time a few wraps of the scanline counter.
  constexpr int kWraps = 4;
  uint64_t wraps[kWraps];
  int count = 0;
  const uint64_t deadline = x86::Rdtsc() + tsc_hz / 8;
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
  if (count < 3) {
    Log("intel: pipe %c scanline %u does not wrap (%d wraps)\n", pipe_name(), last, count);
    return false;
  }
  const uint64_t period = (wraps[count - 1] - wraps[0]) / (count - 1);
  const uint64_t period_us = period * 1000000 / tsc_hz;
  Log("intel: BAR0 %llx, pipe %c, %u lines, frame %llu us\n", static_cast<unsigned long long>(bar), pipe_name(),
      max_line + 1, static_cast<unsigned long long>(period_us));
  if (period_us < 8000 || period_us > 33000) {
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
