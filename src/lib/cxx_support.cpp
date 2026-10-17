// Runtime hooks the C++ compiler may reference in a freestanding build.

#include "drivers/debug_log.h"

// Called if a pure virtual function is invoked during construction or destruction: a bug.
extern "C" void __cxa_pure_virtual() {
  Log("fatal: pure virtual call\n");
  for (;;) {
    asm volatile("cli; hlt");
  }
}
