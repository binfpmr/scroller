#include "drivers/debug_log.h"

#include <stdarg.h>

#include "arch/x86_64/io.h"
#include "lib/format.h"

namespace debug_log {
namespace {

constexpr uint16_t kDebugconPort = 0xe9;
constexpr uint16_t kCom1 = 0x3f8;
constexpr uint8_t kLineStatusTxEmpty = 0x20;

void SerialPut(char c) {
  // Bounded wait: a missing UART reads back 0xFF (looks ready), a stuck one must not hang the game.
  for (int i = 0; i < 100000 && (x86::Inb(kCom1 + 5) & kLineStatusTxEmpty) == 0; ++i) {
    x86::Pause();
  }
  x86::Outb(kCom1, static_cast<uint8_t>(c));
}

// Not thread safe: a Log from an interrupt handler in the middle of one from the main loop can garble a few bytes.
char g_history[kHistorySize];
uint64_t g_history_end = 0;

}  // namespace

void Init() {
  x86::Outb(kCom1 + 1, 0x00);  // No interrupts.
  x86::Outb(kCom1 + 3, 0x80);  // DLAB on.
  x86::Outb(kCom1 + 0, 0x01);  // Divisor 1 = 115200 baud.
  x86::Outb(kCom1 + 1, 0x00);
  x86::Outb(kCom1 + 3, 0x03);  // 8N1, DLAB off.
  x86::Outb(kCom1 + 2, 0xc7);  // FIFO on, cleared, 14-byte threshold.
  x86::Outb(kCom1 + 4, 0x03);  // DTR + RTS.
}

void Write(const char* text, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    x86::Outb(kDebugconPort, static_cast<uint8_t>(text[i]));
    if (text[i] == '\n') {
      SerialPut('\r');
    }
    SerialPut(text[i]);
    g_history[g_history_end++ % kHistorySize] = text[i];
  }
}

uint64_t HistoryEnd() { return g_history_end; }

size_t ReadHistory(uint64_t* position, char* out, size_t max) {
  const uint64_t end = g_history_end;
  if (end - *position > kHistorySize) {
    *position = end - kHistorySize;
  }
  size_t n = 0;
  while (n < max && *position + n < end) {
    out[n] = g_history[(*position + n) % kHistorySize];
    ++n;
  }
  return n;
}

}  // namespace debug_log

void Log(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  size_t n = lib::FormatV(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n >= sizeof(buf)) {
    n = sizeof(buf) - 1;
  }
  debug_log::Write(buf, n);
}
