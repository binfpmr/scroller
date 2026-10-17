// CPU helpers: timestamp counter, CPUID, interrupt flag.
#pragma once

#include <stdint.h>

namespace x86 {

inline uint64_t Rdtsc() {
  uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

struct CpuidResult {
  uint32_t eax, ebx, ecx, edx;
};

inline CpuidResult Cpuid(uint32_t leaf, uint32_t subleaf = 0) {
  CpuidResult r;
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx), "=c"(r.ecx), "=d"(r.edx) : "a"(leaf), "c"(subleaf));
  return r;
}

// Invariant TSC: constant rate across P-/C-states (CPUID 8000_0007h EDX bit 8).
inline bool HasInvariantTsc() {
  if (Cpuid(0x80000000).eax < 0x80000007) {
    return false;
  }
  return (Cpuid(0x80000007).edx & (1u << 8)) != 0;
}

inline void DisableInterrupts() { asm volatile("cli" ::: "memory"); }

}  // namespace x86
