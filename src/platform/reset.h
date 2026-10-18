#pragma once

#include "uefi/uefi.h"

namespace platform {

// Reboot: UEFI ResetSystem (warm unless `cold`), then the i8042 reset line, then a triple fault. Either way the
// firmware starts over; on the HP Pavilion that stops at a false fan error (90B) until Enter is pressed.
[[noreturn]] void Reboot(EFI_RUNTIME_SERVICES* runtime_services, bool cold = false);

}  // namespace platform
