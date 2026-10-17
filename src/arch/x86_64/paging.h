// Queries on the current (firmware-built, identity-mapped) page tables.
#pragma once

#include <stdint.h>

namespace x86 {

// True if the 4 KiB page containing `address` is mapped in the current 4-level page tables. Touching an unmapped
// address would page-fault, and with no exception handlers yet that resets the machine.
bool IsMapped(uint64_t address);

}  // namespace x86
