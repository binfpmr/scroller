#pragma once

#include "uefi/uefi.h"

namespace platform {

// Reboot: UEFI ResetSystem, then the i8042 reset line, then a triple fault. Warm by default: on the HP Pavilion a
// cold start runs the fan check, which (falsely) stops at "System Fan (90B)" until Enter is pressed.
[[noreturn]] void Reboot(EFI_RUNTIME_SERVICES* runtime_services, bool cold = false);

}  // namespace platform
