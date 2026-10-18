#include "drivers/local_apic.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/msr.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"

namespace lapic {
namespace {

constexpr uint32_t kMsrApicBase = 0x1b;
constexpr uint64_t kApicBaseEnable = 1ull << 11;
constexpr uint64_t kApicBaseX2 = 1ull << 10;

// Register offsets (xAPIC MMIO); x2APIC uses MSR 0x800 + offset / 16.
constexpr uint32_t kId = 0x20;
constexpr uint32_t kTaskPriority = 0x80;
constexpr uint32_t kEoi = 0xb0;
constexpr uint32_t kSpurious = 0xf0;
constexpr uint32_t kLvtTimer = 0x320;
constexpr uint32_t kTimerInitial = 0x380;
constexpr uint32_t kTimerCurrent = 0x390;
constexpr uint32_t kTimerDivide = 0x3e0;

constexpr uint32_t kLvtMasked = 1u << 16;
constexpr uint32_t kLvtPeriodic = 1u << 17;
constexpr uint32_t kDivideBy16 = 0x3;

volatile uint32_t* g_mmio = nullptr;
bool g_x2apic = false;

uint32_t Read(uint32_t reg) {
  if (g_x2apic) {
    return static_cast<uint32_t>(x86::Rdmsr(0x800 + reg / 16));
  }
  return g_mmio[reg / 4];
}

void Write(uint32_t reg, uint32_t value) {
  if (g_x2apic) {
    x86::Wrmsr(0x800 + reg / 16, value);
  } else {
    g_mmio[reg / 4] = value;
  }
}

}  // namespace

bool Init(uint64_t mmio_address, lib::Arena& arena) {
  if ((x86::Cpuid(1).edx & (1u << 9)) == 0) {
    Log("lapic: none\n");
    return false;
  }
  uint64_t base = x86::Rdmsr(kMsrApicBase);
  if ((base & kApicBaseEnable) == 0) {
    base |= kApicBaseEnable;
    x86::Wrmsr(kMsrApicBase, base);
  }
  g_x2apic = (base & kApicBaseX2) != 0;
  if (!g_x2apic) {
    if (mmio_address == 0) {
      mmio_address = base & 0x000ffffffffff000ull;
    }
    if (!x86::IsMapped(mmio_address) && !x86::MapUncached(arena, mmio_address, 4096)) {
      Log("lapic: cannot map %llx\n", static_cast<unsigned long long>(mmio_address));
      return false;
    }
    g_mmio = reinterpret_cast<volatile uint32_t*>(mmio_address);
  }
  Write(kTaskPriority, 0);
  Write(kSpurious, 0x100 | kSpuriousVector);  // software enable
  Log("lapic: %s mode, id %u\n", g_x2apic ? "x2APIC" : "xAPIC", Id());
  return true;
}

bool x2apic() { return g_x2apic; }

uint32_t Id() { return g_x2apic ? Read(kId) : Read(kId) >> 24; }

void EndOfInterrupt() { Write(kEoi, 0); }

uint32_t StartPeriodicTimer(uint8_t vector, uint32_t hz, uint64_t tsc_hz) {
  // Count down from the maximum for 10 ms of TSC time to learn the timer's input rate.
  Write(kLvtTimer, kLvtMasked);
  Write(kTimerDivide, kDivideBy16);
  Write(kTimerInitial, 0xffffffffu);
  const uint64_t end = x86::Rdtsc() + tsc_hz / 100;
  while (x86::Rdtsc() < end) {
    x86::Pause();
  }
  const uint32_t elapsed = 0xffffffffu - Read(kTimerCurrent);
  Write(kTimerInitial, 0);
  const uint64_t timer_hz = static_cast<uint64_t>(elapsed) * 100;
  if (timer_hz < hz) {
    Log("lapic: timer does not count (%u in 10 ms)\n", elapsed);
    return 0;
  }
  const uint32_t count = static_cast<uint32_t>(timer_hz / hz);
  Write(kLvtTimer, kLvtPeriodic | vector);
  Write(kTimerInitial, count);
  Log("lapic: timer input %llu kHz (after /16), %u Hz on vector %u\n", static_cast<unsigned long long>(timer_hz / 1000),
      static_cast<uint32_t>(timer_hz / count), vector);
  return static_cast<uint32_t>(timer_hz / count);
}

}  // namespace lapic
