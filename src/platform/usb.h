// USB: the first xHCI controller, its root ports and the devices on them.
#pragma once

#include "lib/arena.h"
#include "platform/boot_info.h"

namespace platform {

// Only when the game owns the machine. Logs the controller and its ports.
void InitUsb(const BootInfo& boot, lib::Arena& arena);

// For the stats overlay, e.g. "XHCI 8086:A36D 16 PORTS" or "NONE".
const char* UsbStatus();

}  // namespace platform
