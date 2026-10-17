#include "lib/format.h"

namespace lib {
namespace {

class Output {
 public:
  Output(char* buf, size_t size) : buf_(buf), size_(size) {}

  void Put(char c) {
    if (pos_ + 1 < size_) {
      buf_[pos_] = c;
    }
    ++pos_;
  }

  void Pad(char c, int count) {
    for (int i = 0; i < count; ++i) {
      Put(c);
    }
  }

  size_t Finish() {
    if (size_ > 0) {
      buf_[pos_ < size_ ? pos_ : size_ - 1] = '\0';
    }
    return pos_;
  }

 private:
  char* buf_;
  size_t size_;
  size_t pos_ = 0;
};

struct Spec {
  bool left = false;
  bool zero = false;
  int width = 0;
};

void PutNumber(Output& out, unsigned long long value, unsigned base, bool upper, bool negative, const Spec& spec) {
  const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char tmp[24];
  int n = 0;
  do {
    tmp[n++] = digits[value % base];
    value /= base;
  } while (value != 0);

  const int len = n + (negative ? 1 : 0);
  const int pad = spec.width > len ? spec.width - len : 0;
  if (!spec.left && !spec.zero) {
    out.Pad(' ', pad);
  }
  if (negative) {
    out.Put('-');
  }
  if (!spec.left && spec.zero) {
    out.Pad('0', pad);
  }
  while (n > 0) {
    out.Put(tmp[--n]);
  }
  if (spec.left) {
    out.Pad(' ', pad);
  }
}

void PutString(Output& out, const char* s, const Spec& spec) {
  if (s == nullptr) {
    s = "(null)";
  }
  int len = 0;
  while (s[len] != '\0') {
    ++len;
  }
  const int pad = spec.width > len ? spec.width - len : 0;
  if (!spec.left) {
    out.Pad(' ', pad);
  }
  for (int i = 0; i < len; ++i) {
    out.Put(s[i]);
  }
  if (spec.left) {
    out.Pad(' ', pad);
  }
}

}  // namespace

size_t FormatV(char* buf, size_t size, const char* fmt, va_list args) {
  Output out(buf, size);
  for (const char* p = fmt; *p != '\0'; ++p) {
    if (*p != '%') {
      out.Put(*p);
      continue;
    }
    ++p;

    Spec spec;
    for (;; ++p) {
      if (*p == '-') {
        spec.left = true;
      } else if (*p == '0') {
        spec.zero = true;
      } else {
        break;
      }
    }
    while (*p >= '0' && *p <= '9') {
      spec.width = spec.width * 10 + (*p - '0');
      ++p;
    }
    int longs = 0;
    while (*p == 'l' || *p == 'z') {
      longs += *p == 'z' ? 2 : 1;
      ++p;
    }

    switch (*p) {
      case 'd':
      case 'i': {
        long long v = longs >= 2 ? va_arg(args, long long) : longs == 1 ? va_arg(args, long) : va_arg(args, int);
        const bool negative = v < 0;
        const unsigned long long magnitude = negative ? 0ull - static_cast<unsigned long long>(v) : v;
        PutNumber(out, magnitude, 10, false, negative, spec);
        break;
      }
      case 'u':
      case 'x':
      case 'X': {
        unsigned long long v = longs >= 2   ? va_arg(args, unsigned long long)
                               : longs == 1 ? va_arg(args, unsigned long)
                                            : va_arg(args, unsigned int);
        PutNumber(out, v, *p == 'u' ? 10 : 16, *p == 'X', false, spec);
        break;
      }
      case 'p': {
        out.Put('0');
        out.Put('x');
        PutNumber(out, reinterpret_cast<unsigned long long>(va_arg(args, void*)), 16, false, false,
                  Spec{.left = false, .zero = true, .width = 16});
        break;
      }
      case 's':
        PutString(out, va_arg(args, const char*), spec);
        break;
      case 'c':
        out.Put(static_cast<char>(va_arg(args, int)));
        break;
      case '%':
        out.Put('%');
        break;
      case '\0':
        --p;  // Lone '%' at the end: stop at the terminator.
        break;
      default:
        out.Put('%');
        out.Put(*p);
        break;
    }
  }
  return out.Finish();
}

size_t Format(char* buf, size_t size, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const size_t n = FormatV(buf, size, fmt, args);
  va_end(args);
  return n;
}

}  // namespace lib
