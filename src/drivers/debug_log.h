// Debug log written to the QEMU debug console (port 0xE9) and the first serial port (COM1, 115200 8N1).
// Usable both before and after ExitBootServices; on hardware without these ports the writes are harmless.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace debug_log {

void Init();
void Write(const char* text, size_t length);

// Everything written is also kept in a ring of the last kHistorySize bytes, for the network debug link. Positions
// count bytes since boot.
inline constexpr size_t kHistorySize = 32 * 1024;
uint64_t HistoryEnd();
// Copies up to `max` bytes starting at `*position` (moved forward to the oldest byte still kept if it was older).
// Returns the number of bytes copied.
size_t ReadHistory(uint64_t* position, char* out, size_t max);

}  // namespace debug_log

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
