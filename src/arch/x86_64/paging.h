// Queries on and additions to the current identity-mapped page tables (built by the firmware).
#pragma once

#include <stdint.h>

#include "lib/arena.h"

namespace x86 {

// True if the 4 KiB page containing `address` is mapped in the current 4-level page tables. Touching an unmapped
// address would page-fault, and with no exception handlers yet that resets the machine.
bool IsMapped(uint64_t address);

// Identity-maps [base, base + size) as uncached 2 MiB pages wherever nothing is mapped yet (existing mappings are left
// alone), allocating page tables from `arena`. For device registers (MMIO) the firmware did not map. Only valid after
// ExitBootServices. Returns false if a table could not be allocated or 5-level paging is on.
bool MapUncached(lib::Arena& arena, uint64_t base, uint64_t size);

}  // namespace x86
