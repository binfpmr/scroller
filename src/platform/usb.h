// USB: the first xHCI controller and the devices on its root ports at boot (no hubs, no hot-plug yet). HID boot
// keyboards feed the same InputTracker as the PS/2 keyboard.
#pragma once

#include <stdint.h>

#include "lib/arena.h"
#include "platform/boot_info.h"
#include "platform/input.h"

namespace platform {

// Only when the game owns the machine. Logs the controller and its ports.
void InitUsb(const BootInfo& boot, lib::Arena& arena);

// Key reports from USB keyboards (polled, from the main loop's input polling).
void PollUsb(InputTracker& tracker);
// Halts the controller (before a hot reload).
void StopUsb();
// Last newly pressed key's HID usage ID (for the overlay), 0 if none yet.
uint8_t UsbLastKey();

// For the stats overlay, e.g. "XHCI 8086:A36D 16 PORTS  2 DEV  1 KBD" or "NONE".
const char* UsbStatus();

}  // namespace platform
