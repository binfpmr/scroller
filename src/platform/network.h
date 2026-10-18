// Network: finds a supported Ethernet controller, runs the net::Stack on it (address from the PXE boot, else DHCP)
// and polls it from the main loop.
#pragma once

#include "lib/arena.h"
#include "net/stack.h"
#include "platform/boot_info.h"

namespace platform {

// Logs what it finds. Without a supported controller the network stays off (NetworkStack() returns null).
void InitNetwork(const BootInfo& boot, lib::Arena& arena);

// Hands received frames to the stack and runs its timers. Call often (every main loop iteration).
void PollNetwork();

net::Stack* NetworkStack();

// True while PollNetwork runs (the debugger must not use the network when it stops the machine in there).
bool NetworkBusy();

// For the stats overlay, e.g. "ETH 10EC:8168 1000M 192.168.178.40" or "ETH 8086:100E NO LINK".
const char* NetworkStatus();
// Driver register state (empty for drivers without one), shown under the NET line.
const char* NetworkDetail();

}  // namespace platform
