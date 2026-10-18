#pragma once

#include "uefi/uefi.h"

namespace platform {

// How to reset. Every one of them starts the firmware over; they differ in how much of the machine is reset (on
// the HP Pavilion UEFI warm and cold both stop at a false fan error, 90B, until Enter is pressed).
enum class ResetMethod : uint8_t {
  kWarm = 0,      // UEFI ResetSystem(EfiResetWarm)
  kCold = 1,      // UEFI ResetSystem(EfiResetCold)
  kSoft = 2,      // chipset reset control register 0xCF9 = 0x04: CPU reset only (no SYS_RST)
  kKeyboard = 3,  // i8042 command 0xFE: pulse the CPU reset line
};

// Resets with `method`; if that does not happen, falls back to the i8042 reset line, then a triple fault.
[[noreturn]] void Reboot(EFI_RUNTIME_SERVICES* runtime_services, ResetMethod method = ResetMethod::kWarm);

}  // namespace platform
