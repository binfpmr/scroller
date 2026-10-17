// Vertical blank detection through the legacy VGA input status register (port 0x3DA, bit 3 = vertical retrace).
// Many GPUs still emulate it in UEFI mode, some do not, and QEMU's VGA flips the bit on every read; Probe() only
// accepts it if it shows a plausible, steady display refresh.
#pragma once

#include <stdint.h>

namespace vga {

// Watches the retrace bit for up to ~120 ms. Returns true and the refresh period in TSC cycles if it looks real.
bool ProbeRetrace(uint64_t tsc_hz, uint64_t* period_cycles);

// Current state of the retrace bit.
bool InRetrace();

}  // namespace vga
