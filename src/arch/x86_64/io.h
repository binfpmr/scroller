// Port I/O and small CPU helpers.
#pragma once

#include <stdint.h>

namespace x86 {

inline void Outb(uint16_t port, uint8_t value) { asm volatile("outb %0, %1" : : "a"(value), "Nd"(port)); }

inline uint8_t Inb(uint16_t port) {
  uint8_t value;
  asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
  return value;
}

inline void Pause() { asm volatile("pause"); }

inline void Halt() { asm volatile("hlt"); }

}  // namespace x86
