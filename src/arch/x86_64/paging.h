// Queries on and additions to the current identity-mapped page tables (built by the firmware).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lib/arena.h"

namespace x86 {

// True if the 4 KiB page containing `address` is mapped in the current 4-level page tables. Touching an unmapped
// address would page-fault, and with no exception handlers yet that resets the machine.
bool IsMapped(uint64_t address);
// Copies into memory even if its pages are mapped read-only (e.g. our code, for breakpoints): clears CR0.WP for the
// copy, with interrupts off. The range must be mapped.
void CopyIgnoringWriteProtect(void* destination, const void* source, size_t length);
// True if every page of [address, address + length) is mapped (an empty range is).
bool RangeMapped(uint64_t address, uint64_t length);

// Where the page tables we add come from (MapUncached, EnableWriteCombining). Not the game's arena: those tables
// must outlive a hot reload, which reuses the arena (platform/reload).
void SetPageTableArena(lib::Arena* arena);
lib::Arena* PageTableArena();

// Identity-maps [base, base + size) as uncached 2 MiB pages wherever nothing is mapped yet (existing mappings are left
// alone), allocating tables from the page table arena. For device registers (MMIO) the firmware did not map. Only
// valid after ExitBootServices. Returns false if a table could not be allocated or 5-level paging is on.
bool MapUncached(uint64_t base, uint64_t size);

// Clears the no-execute bit on every level that maps [base, base + size) (for code loaded at run time) and flushes
// the TLB. Returns false if part of the range is not mapped.
bool AllowExecute(uint64_t base, uint64_t size);

}  // namespace x86
