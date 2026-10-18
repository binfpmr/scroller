#include "platform/interrupt_controllers.h"

#include "drivers/debug_log.h"
#include "drivers/io_apic.h"
#include "drivers/local_apic.h"
#include "drivers/pic.h"

namespace platform {
namespace {

volatile uint64_t g_timer_ticks = 0;
volatile uint64_t g_unexpected = 0;
uint32_t g_timer_hz = 0;
const acpi::Madt* g_madt = nullptr;
ioapic::IoApic g_io_apics[acpi::Madt::kMaxIoApics];
int g_io_apic_count = 0;

void OnTimer(x86::InterruptFrame&) {
  g_timer_ticks = g_timer_ticks + 1;
  lapic::EndOfInterrupt();
}

void OnUnexpected(x86::InterruptFrame& frame) {
  g_unexpected = g_unexpected + 1;
  if (g_unexpected <= 8) {
    Log("interrupt: unexpected vector %llu\n", static_cast<unsigned long long>(frame.vector));
  }
  lapic::EndOfInterrupt();  // otherwise the APIC would block this and all lower-priority vectors
}

void OnSpurious(x86::InterruptFrame&) {}  // no EOI for spurious interrupts

}  // namespace

bool SetUpInterruptControllers(const acpi::Madt* madt, lib::Arena& arena, uint64_t tsc_hz) {
  g_madt = madt;
  for (int v = 0x30; v < 0xff; ++v) {
    x86::SetInterruptHandler(static_cast<uint8_t>(v), &OnUnexpected);
  }
  x86::SetInterruptHandler(lapic::kSpuriousVector, &OnSpurious);
  x86::SetInterruptHandler(pic::kVectorBase + 7, &OnSpurious);
  x86::SetInterruptHandler(pic::kVectorBase + 15, &OnSpurious);

  if (madt == nullptr || madt->has_8259) {
    pic::RemapAndMaskAll();
  }
  if (!lapic::Init(madt != nullptr ? madt->local_apic_address : 0, arena)) {
    return false;
  }
  for (int i = 0; madt != nullptr && i < madt->io_apic_count; ++i) {
    if (g_io_apics[g_io_apic_count].Init(madt->io_apics[i].address, madt->io_apics[i].gsi_base, arena)) {
      ++g_io_apic_count;
    }
  }
  x86::SetInterruptHandler(kTimerVector, &OnTimer);
  g_timer_hz = lapic::StartPeriodicTimer(kTimerVector, kTimerHz, tsc_hz);
  x86::EnableInterrupts();
  return true;
}

uint64_t TimerTicks() { return g_timer_ticks; }
uint32_t TimerHz() { return g_timer_hz; }
uint64_t UnexpectedInterrupts() { return g_unexpected; }

bool RouteIsaIrq(uint8_t irq, uint8_t vector, x86::InterruptHandler handler) {
  if (g_madt == nullptr) {
    return false;
  }
  uint16_t flags = 0;
  const uint32_t gsi = g_madt->GsiForIsaIrq(irq, &flags);
  for (int i = 0; i < g_io_apic_count; ++i) {
    if (g_io_apics[i].Handles(gsi)) {
      x86::SetInterruptHandler(vector, handler);
      g_io_apics[i].Route(gsi, vector, flags, lapic::Id());
      Log("irq: ISA IRQ %u -> GSI %u -> vector %u\n", irq, gsi, vector);
      return true;
    }
  }
  return false;
}

void MaskIsaIrq(uint8_t irq) {
  if (g_madt == nullptr) {
    return;
  }
  uint16_t flags = 0;
  const uint32_t gsi = g_madt->GsiForIsaIrq(irq, &flags);
  for (int i = 0; i < g_io_apic_count; ++i) {
    if (g_io_apics[i].Handles(gsi)) {
      g_io_apics[i].Mask(gsi);
    }
  }
}

}  // namespace platform
