// The CPU's local APIC, in xAPIC (MMIO) or x2APIC (MSR) mode, whichever the firmware left enabled. Provides
// end-of-interrupt and a periodic timer.
#pragma once

#include <stdint.h>

#include "lib/arena.h"

namespace lapic {

inline constexpr uint8_t kSpuriousVector = 0xff;

// Enables the APIC (spurious vector 0xFF). `mmio_address` comes from the MADT; in xAPIC mode it is mapped uncached
// if the firmware did not map it. Returns false if there is no usable APIC.
bool Init(uint64_t mmio_address, lib::Arena& arena);

bool x2apic();
uint32_t Id();
void EndOfInterrupt();

// Starts a periodic timer interrupt at about `hz` on `vector`, calibrated against the TSC. Returns the rate actually
// programmed (0 on failure).
uint32_t StartPeriodicTimer(uint8_t vector, uint32_t hz, uint64_t tsc_hz);

}  // namespace lapic
