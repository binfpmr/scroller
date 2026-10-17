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

// Running in a virtual machine (CPUID 1 ECX bit 31, set by hypervisors).
inline bool UnderHypervisor() { return (Cpuid(1).ecx & (1u << 31)) != 0; }

// Invariant TSC: constant rate across P-/C-states (CPUID 8000_0007h EDX bit 8).
inline bool HasInvariantTsc() {
  if (Cpuid(0x80000000).eax < 0x80000007) {
    return false;
  }
  return (Cpuid(0x80000007).edx & (1u << 8)) != 0;
}

// CPU brand string (CPUID 8000_0002h..8000_0004h), leading spaces removed; "unknown" if not available.
inline void CpuBrand(char (&out)[49]) {
  if (Cpuid(0x80000000).eax < 0x80000004) {
    __builtin_memcpy(out, "unknown", 8);
    return;
  }
  uint32_t words[12];
  for (uint32_t i = 0; i < 3; ++i) {
    const CpuidResult r = Cpuid(0x80000002 + i);
    words[i * 4 + 0] = r.eax;
    words[i * 4 + 1] = r.ebx;
    words[i * 4 + 2] = r.ecx;
    words[i * 4 + 3] = r.edx;
  }
  const char* text = reinterpret_cast<const char*>(words);
  int start = 0;
  while (start < 48 && text[start] == ' ') {
    ++start;
  }
  int n = 0;
  for (int i = start; i < 48 && text[i] != '\0'; ++i) {
    out[n++] = text[i];
  }
  out[n] = '\0';
}

inline void DisableInterrupts() { asm volatile("cli" ::: "memory"); }

}  // namespace x86
