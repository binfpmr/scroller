// Debug log written to the QEMU debug console (port 0xE9) and the first serial port (COM1, 115200 8N1).
// Usable both before and after ExitBootServices; on hardware without these ports the writes are harmless.
#pragma once

#include <stddef.h>

namespace debug_log {

void Init();
void Write(const char* text, size_t length);

}  // namespace debug_log

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
