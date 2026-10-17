#include "drivers/vga_retrace.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"

namespace vga {
namespace {

constexpr uint16_t kInputStatus1 = 0x3da;
constexpr uint8_t kVerticalRetrace = 0x08;

bool InRetrace() { return (x86::Inb(kInputStatus1) & kVerticalRetrace) != 0; }

}  // namespace

bool ProbeRetrace(uint64_t tsc_hz, uint64_t* period_cycles) {
  constexpr int kEdges = 6;
  uint64_t edges[kEdges];
  int count = 0;
  const uint64_t start = x86::Rdtsc();
  const uint64_t deadline = start + tsc_hz / 8;
  bool was = InRetrace();
  while (count < kEdges && x86::Rdtsc() < deadline) {
    const bool now = InRetrace();
    if (now && !was) {
      edges[count++] = x86::Rdtsc();
    }
    was = now;
  }
  if (count < 4) {
    Log("vsync: port 0x3da shows no retrace (%d edges)\n", count);
    return false;
  }
  // Steady period between 30 and 125 Hz (QEMU's bit flips on every read, far faster).
  uint64_t min = ~0ull;
  uint64_t max = 0;
  for (int i = 1; i < count; ++i) {
    const uint64_t d = edges[i] - edges[i - 1];
    min = d < min ? d : min;
    max = d > max ? d : max;
  }
  const uint64_t mean = (edges[count - 1] - edges[0]) / (count - 1);
  const uint64_t mean_us = mean * 1000000 / tsc_hz;
  const bool plausible = mean_us >= 8000 && mean_us <= 33000 && (max - min) * 10 <= mean;
  Log("vsync: port 0x3da retrace period %llu us (min %llu, max %llu) -> %s\n", static_cast<unsigned long long>(mean_us),
      static_cast<unsigned long long>(min * 1000000 / tsc_hz), static_cast<unsigned long long>(max * 1000000 / tsc_hz),
      plausible ? "using it" : "not plausible");
  if (plausible) {
    *period_cycles = mean;
  }
  return plausible;
}

void WaitForRetraceStart(uint64_t timeout_cycles) {
  const uint64_t deadline = x86::Rdtsc() + timeout_cycles;
  while (InRetrace() && x86::Rdtsc() < deadline) {
    x86::Pause();
  }
  while (!InRetrace() && x86::Rdtsc() < deadline) {
    x86::Pause();
  }
}

}  // namespace vga
