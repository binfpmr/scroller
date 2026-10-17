#pragma once

#include "uefi/uefi.h"

namespace platform {

// Cold reboot: UEFI ResetSystem, then the i8042 reset line, then a triple fault.
[[noreturn]] void Reboot(EFI_RUNTIME_SERVICES* runtime_services);

}  // namespace platform
