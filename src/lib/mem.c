// Freestanding memory functions. The compiler may emit calls to these on its own (struct copies, zero-init), so they
// must exist with exactly these C names.
#include <stddef.h>

void* memset(void* dst, int c, size_t n) {
  void* d = dst;
  __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
  return dst;
}

void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
  void* d = dst;
  __asm__ volatile("rep movsb" : "+D"(d), "+S"(src), "+c"(n) : : "memory");
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = dst;
  const unsigned char* s = src;
  if (d <= s || d >= s + n) {
    return memcpy(dst, src, n);
  }
  // Overlapping with dst above src: copy backwards.
  while (n > 0) {
    --n;
    d[n] = s[n];
  }
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = a;
  const unsigned char* y = b;
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) {
      return x[i] < y[i] ? -1 : 1;
    }
  }
  return 0;
}

size_t strlen(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}
