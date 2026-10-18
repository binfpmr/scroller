// Interrupt controller setup after ExitBootServices: legacy PICs masked, local APIC enabled with a periodic timer,
// IOAPIC inputs masked until a driver routes one.
#pragma once

#include <stdint.h>

#include "arch/x86_64/interrupts.h"
#include "drivers/acpi.h"

namespace platform {

inline constexpr uint8_t kTimerVector = 0x40;
inline constexpr uint8_t kKeyboardVector = 0x41;
inline constexpr uint32_t kTimerHz = 1000;

// Requires x86::InitInterrupts. Enables interrupts at the end. False (interrupts left off) if there is no local APIC.
// `madt` may be null (no ACPI): then the APIC address comes from the CPU and no IOAPIC is used.
bool SetUpInterruptControllers(const acpi::Madt* madt, uint64_t tsc_hz);

// Timer interrupts since setup (1 kHz); 0 if the timer is not running.
uint64_t TimerTicks();
// Called from the timer interrupt at every tick (e.g. to refill an audio buffer); null removes it.
void SetTimerCallback(void (*callback)());
uint32_t TimerHz();

// Interrupts on vectors nobody handles (each is acknowledged and counted).
uint64_t UnexpectedInterrupts();

// Routes an ISA IRQ (applying the MADT override) to `vector` and installs `handler`, which must call
// lapic::EndOfInterrupt(). False if no IOAPIC covers it.
bool RouteIsaIrq(uint8_t irq, uint8_t vector, x86::InterruptHandler handler);
void MaskIsaIrq(uint8_t irq);

// Before a hot reload: masks every IOAPIC input and stops the timer, lets interrupts already on their way arrive at
// the current handlers (1 ms with interrupts on), then leaves interrupts off. Otherwise a pending one would reach the
// next image before it has installed its handlers.
void QuiesceInterrupts(uint64_t tsc_hz);

}  // namespace platform
