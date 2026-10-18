#include "platform/reset.h"

#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"

namespace platform {

void Reboot(EFI_RUNTIME_SERVICES* runtime_services, bool cold) {
  Log("reboot (%s)\n", cold ? "cold" : "warm");
  if (runtime_services != nullptr) {
    runtime_services->ResetSystem(cold ? EfiResetCold : EfiResetWarm, EFI_SUCCESS, 0, nullptr);
  }

  Log("ResetSystem returned, trying the keyboard controller\n");
  for (int i = 0; i < 100000 && (x86::Inb(0x64) & 0x02) != 0; ++i) {
    x86::Pause();
  }
  x86::Outb(0x64, 0xfe);  // pulse the CPU reset line
  for (int i = 0; i < 10000000; ++i) {
    x86::Pause();
  }

  Log("still running, forcing a triple fault\n");
  struct [[gnu::packed]] {
    uint16_t limit;
    uint64_t base;
  } empty_idt = {0, 0};
  asm volatile("lidt %0; int3" : : "m"(empty_idt));
  for (;;) {
    x86::Halt();
  }
}

}  // namespace platform
