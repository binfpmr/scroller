// Device register access through a memory-mapped BAR, and a short busy-wait usable before the TSC is calibrated.
#pragma once

#include <stdint.h>

#include "arch/x86_64/io.h"

namespace mmio {

template <typename T>
T Read(volatile uint8_t* base, uint32_t offset) {
  return *reinterpret_cast<volatile T*>(base + offset);
}

template <typename T>
void Write(volatile uint8_t* base, uint32_t offset, T value) {
  *reinterpret_cast<volatile T*>(base + offset) = value;
}

// Port 0x80 writes take about 1 us each.
inline void DelayUs(uint64_t microseconds) {
  for (uint64_t i = 0; i < microseconds; ++i) {
    x86::Outb(0x80, 0);
  }
}

}  // namespace mmio
