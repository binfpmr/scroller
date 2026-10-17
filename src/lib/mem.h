// Memory helpers usable in both the freestanding and the host build. They go through compiler builtins, which become
// inline code or calls to memcpy/memset (lib/mem.c on the target, libc on the host).
#pragma once

#include <stddef.h>

namespace lib {

inline void Copy(void* dst, const void* src, size_t n) { __builtin_memcpy(dst, src, n); }
inline void Fill(void* dst, int value, size_t n) { __builtin_memset(dst, value, n); }

template <typename T>
constexpr T Min(T a, T b) {
  return b < a ? b : a;
}

template <typename T>
constexpr T Max(T a, T b) {
  return a < b ? b : a;
}

template <typename T>
constexpr T Clamp(T value, T lo, T hi) {
  return value < lo ? lo : hi < value ? hi : value;
}

}  // namespace lib
