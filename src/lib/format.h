// printf-style formatting into a caller-provided buffer.
//
// Supported: %d %i %u %x %X %p %s %c %%, flags '-' (left-align) and '0' (zero-pad), a decimal width, and the length
// modifiers l, ll and z. Like snprintf: the output is always NUL-terminated when size > 0 and the return value is the
// length the full output would have had.
#pragma once

#include <stdarg.h>
#include <stddef.h>

namespace lib {

size_t FormatV(char* buf, size_t size, const char* fmt, va_list args);
size_t Format(char* buf, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

}  // namespace lib
