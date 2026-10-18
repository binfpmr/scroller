#include "arch/x86_64/paging.h"

#include "arch/x86_64/msr.h"
#include "lib/mem.h"

namespace x86 {
namespace {

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kWritable = 1ull << 1;
constexpr uint64_t kPwt = 1ull << 3;
constexpr uint64_t kPcd = 1ull << 4;
constexpr uint64_t kLargePage = 1ull << 7;
constexpr uint64_t kAddressMask = 0x000ffffffffff000ull;
constexpr uint64_t kCr0WriteProtect = 1ull << 16;
constexpr uint64_t kCr4La57 = 1ull << 12;
constexpr uint64_t kSize2M = 1ull << 21;

lib::Arena* g_page_table_arena = nullptr;
constexpr uint64_t kNoExecute = 1ull << 63;

uint64_t* TableAt(uint64_t entry) { return reinterpret_cast<uint64_t*>(entry & kAddressMask); }

// Returns the next-level table behind `entry`, creating an empty one if the entry is not present. Null if the entry
// is a large page (already mapped) or allocation fails.
uint64_t* NextTable(uint64_t* entry) {
  if ((*entry & kPresent) == 0) {
    auto* table =
        g_page_table_arena != nullptr ? static_cast<uint64_t*>(g_page_table_arena->Allocate(4096, 4096)) : nullptr;
    if (table == nullptr) {
      return nullptr;
    }
    for (int i = 0; i < 512; ++i) {
      table[i] = 0;
    }
    *entry = reinterpret_cast<uint64_t>(table) | kPresent | kWritable;
  } else if ((*entry & kLargePage) != 0) {
    return nullptr;
  }
  return TableAt(*entry);
}

}  // namespace

void SetPageTableArena(lib::Arena* arena) { g_page_table_arena = arena; }
lib::Arena* PageTableArena() { return g_page_table_arena; }

bool AllowExecute(uint64_t base, uint64_t size) {
  if ((ReadCr4() & kCr4La57) != 0) {
    return false;
  }
  const uint64_t cr0 = ReadCr0();
  WriteCr0(cr0 & ~kCr0WriteProtect);  // firmware may have made its page tables read-only
  bool mapped = true;
  for (uint64_t page = base & ~0xfffull; page < base + size && mapped; page += 0x1000) {
    uint64_t* table = TableAt(ReadCr3());
    for (int shift = 39; shift >= 12; shift -= 9) {
      uint64_t& entry = table[(page >> shift) & 511];
      if ((entry & kPresent) == 0) {
        mapped = false;
        break;
      }
      entry &= ~kNoExecute;
      if (shift == 12 || ((shift == 30 || shift == 21) && (entry & kLargePage) != 0)) {
        break;
      }
      table = TableAt(entry);
    }
  }
  WriteCr0(cr0);
  WriteCr3(ReadCr3());  // flush the TLB
  return mapped;
}

void CopyIgnoringWriteProtect(void* destination, const void* source, size_t length) {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags)::"memory");
  const uint64_t cr0 = ReadCr0();
  WriteCr0(cr0 & ~kCr0WriteProtect);
  lib::Copy(destination, source, length);
  WriteCr0(cr0);
  if ((flags & (1u << 9)) != 0) {
    asm volatile("sti" ::: "memory");
  }
}

bool RangeMapped(uint64_t address, uint64_t length) {
  if (length == 0) {
    return true;
  }
  if (address + length < address) {
    return false;
  }
  for (uint64_t page = address & ~0xfffull; page < address + length; page += 0x1000) {
    if (!IsMapped(page)) {
      return false;
    }
  }
  return true;
}

bool IsMapped(uint64_t address) {
  if ((ReadCr4() & kCr4La57) != 0) {
    return false;  // 5-level paging: not handled
  }
  const uint64_t* table = TableAt(ReadCr3());
  for (int shift = 39; shift >= 12; shift -= 9) {
    const uint64_t entry = table[(address >> shift) & 511];
    if ((entry & kPresent) == 0) {
      return false;
    }
    if (shift == 12 || ((shift == 30 || shift == 21) && (entry & kLargePage) != 0)) {
      return true;
    }
    table = TableAt(entry);
  }
  return true;
}

bool MapUncached(uint64_t base, uint64_t size) {
  if ((ReadCr4() & kCr4La57) != 0) {
    return false;
  }
  const uint64_t cr0 = ReadCr0();
  WriteCr0(cr0 & ~kCr0WriteProtect);  // firmware may have made its page tables read-only
  bool ok = true;
  for (uint64_t addr = base & ~(kSize2M - 1); addr < base + size; addr += kSize2M) {
    if (IsMapped(addr)) {
      continue;
    }
    uint64_t* pml4e = &TableAt(ReadCr3())[(addr >> 39) & 511];
    uint64_t* pdpt = NextTable(pml4e);
    uint64_t* pd = pdpt != nullptr ? NextTable(&pdpt[(addr >> 30) & 511]) : nullptr;
    if (pd == nullptr) {
      ok = false;
      break;
    }
    uint64_t* pde = &pd[(addr >> 21) & 511];
    if ((*pde & kPresent) == 0) {
      *pde = addr | kPresent | kWritable | kPwt | kPcd | kLargePage;
    }
    // A present PDE pointing to a 4 KiB table with holes is left alone: IsMapped will report those pages.
  }
  FlushTlb();
  WriteCr0(cr0);
  return ok;
}

}  // namespace x86
