// Model-specific registers, control registers and cache control.
#pragma once

#include <stdint.h>

namespace x86 {

inline constexpr uint32_t kMsrMperf = 0xe7;
inline constexpr uint32_t kMsrAperf = 0xe8;
inline constexpr uint32_t kMsrPat = 0x277;
inline constexpr uint32_t kMsrMtrrDefType = 0x2ff;

inline uint64_t Rdmsr(uint32_t msr) {
  uint32_t lo, hi;
  asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

inline void Wrmsr(uint32_t msr, uint64_t value) {
  asm volatile("wrmsr" : : "c"(msr), "a"(static_cast<uint32_t>(value)), "d"(static_cast<uint32_t>(value >> 32)));
}

inline uint64_t ReadCr0() {
  uint64_t v;
  asm volatile("mov %%cr0, %0" : "=r"(v));
  return v;
}
inline void WriteCr0(uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }

inline uint64_t ReadCr3() {
  uint64_t v;
  asm volatile("mov %%cr3, %0" : "=r"(v));
  return v;
}
inline void WriteCr3(uint64_t v) { asm volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }

inline uint64_t ReadCr4() {
  uint64_t v;
  asm volatile("mov %%cr4, %0" : "=r"(v));
  return v;
}

inline void WriteCr4(uint64_t v) { asm volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }

// Flushes the whole TLB, including global pages (toggling CR4.PGE; a CR3 reload alone keeps those).
inline void FlushTlb() {
  constexpr uint64_t kCr4Pge = 1ull << 7;
  const uint64_t cr4 = ReadCr4();
  if ((cr4 & kCr4Pge) != 0) {
    WriteCr4(cr4 & ~kCr4Pge);
    WriteCr4(cr4);
  } else {
    WriteCr3(ReadCr3());
  }
}

// Writes back and invalidates all caches: required around memory type changes.
inline void Wbinvd() { asm volatile("wbinvd" ::: "memory"); }

}  // namespace x86
