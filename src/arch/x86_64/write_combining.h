// Marks a physical range (the framebuffer) write-combining by editing the firmware's identity-mapped page tables.
//
// Firmware usually maps the framebuffer uncached, so every store goes to the device on its own; write-combining lets
// the CPU merge stores into full bursts, which makes presenting a frame many times faster on real hardware.
// PAT entry 7 (normally UC) is reprogrammed to WC and the framebuffer's page table entries are pointed at it. Large
// pages that only partly cover the range are split first. Only valid after ExitBootServices, with interrupts off.
#pragma once

#include <stdint.h>

#include "lib/arena.h"

namespace x86 {

// Prepares the page table entries (splitting large pages with tables from `arena`) and switches the range to WC.
// Returns false (and changes nothing) if the CPU or the page tables are not what we expect.
bool EnableWriteCombining(lib::Arena& arena, uint64_t base, uint64_t size);

}  // namespace x86
