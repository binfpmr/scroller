#include "arch/x86_64/write_combining.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/msr.h"
#include "drivers/debug_log.h"

namespace x86 {
namespace {

constexpr uint64_t kPresent = 1ull << 0;
constexpr uint64_t kPwt = 1ull << 3;
constexpr uint64_t kPcd = 1ull << 4;
constexpr uint64_t kLargePage = 1ull << 7;  // PS in PDPTE/PDE
constexpr uint64_t kPat4K = 1ull << 7;      // PAT bit in a 4 KiB PTE
constexpr uint64_t kPatLarge = 1ull << 12;  // PAT bit in a 1 GiB / 2 MiB entry
constexpr uint64_t kTableFlags = 0x7;       // present, writable, user: for entries pointing to a table
constexpr uint64_t kAddressMask = 0x000ffffffffff000ull;
constexpr uint64_t kAddressMask2M = 0x000fffffffe00000ull;
constexpr uint64_t kAddressMask1G = 0x000fffffc0000000ull;
constexpr uint64_t kCr0WriteProtect = 1ull << 16;
constexpr uint64_t kCr4La57 = 1ull << 12;

constexpr uint64_t kSize4K = 1ull << 12;
constexpr uint64_t kSize2M = 1ull << 21;
constexpr uint64_t kSize1G = 1ull << 30;

constexpr int kWcPatIndex = 7;  // PAT | PCD | PWT
constexpr uint64_t kPatTypeWc = 0x01;

uint64_t* TableAt(uint64_t entry) { return reinterpret_cast<uint64_t*>(entry & kAddressMask); }

// Memory-type bits selecting PAT entry 7 for a leaf entry.
uint64_t WithWc(uint64_t entry, bool large) { return entry | kPwt | kPcd | (large ? kPatLarge : kPat4K); }

// Replaces a large leaf entry (1 GiB or 2 MiB) by a table of 512 entries one level down with the same attributes.
bool Split(uint64_t* entry, uint64_t page_size, lib::Arena& arena) {
  auto* table = static_cast<uint64_t*>(arena.Allocate(4096, 4096));
  if (table == nullptr) {
    return false;
  }
  const uint64_t old = *entry;
  const uint64_t base = old & (page_size == kSize1G ? kAddressMask1G : kAddressMask2M);
  const uint64_t child_size = page_size / 512;
  uint64_t flags = old & ~(page_size == kSize1G ? kAddressMask1G : kAddressMask2M);
  if (child_size == kSize4K) {
    // 4 KiB PTEs have no PS bit; their PAT bit sits where PS was.
    flags = (flags & ~(kLargePage | kPatLarge)) | ((old & kPatLarge) ? kPat4K : 0);
  }
  for (int i = 0; i < 512; ++i) {
    table[i] = (base + i * child_size) | flags;
  }
  *entry = reinterpret_cast<uint64_t>(table) | kTableFlags;
  return true;
}

struct PendingEntry {
  uint64_t* entry;
  uint64_t write_combining;
};

}  // namespace

bool EnableWriteCombining(lib::Arena& arena, uint64_t base, uint64_t size) {
  if ((Cpuid(1).edx & (1u << 16)) == 0) {
    Log("wc: no PAT support\n");
    return false;
  }
  if ((ReadCr4() & kCr4La57) != 0) {
    Log("wc: 5-level paging not supported\n");
    return false;
  }
  const uint64_t pat = Rdmsr(kMsrPat);
  Log("wc: PAT %llx, MTRR default type %llx\n", static_cast<unsigned long long>(pat),
      static_cast<unsigned long long>(Rdmsr(kMsrMtrrDefType)));

  constexpr int kMaxEntries = 4096;
  PendingEntry* entries = arena.AllocateArray<PendingEntry>(kMaxEntries);
  int count = 0;
  if (entries == nullptr) {
    return false;
  }

  const uint64_t cr0 = ReadCr0();
  WriteCr0(cr0 & ~kCr0WriteProtect);  // firmware may have made its page tables read-only
  uint64_t* pml4 = TableAt(ReadCr3());
  const uint64_t end = (base + size + kSize4K - 1) & ~(kSize4K - 1);
  bool ok = true;
  for (uint64_t addr = base & ~(kSize4K - 1); ok && addr < end;) {
    uint64_t* pml4e = &pml4[(addr >> 39) & 511];
    if ((*pml4e & kPresent) == 0) {
      ok = false;
      break;
    }
    uint64_t* pdpte = &TableAt(*pml4e)[(addr >> 30) & 511];
    if ((*pdpte & kPresent) == 0) {
      ok = false;
      break;
    }
    uint64_t* leaf = nullptr;
    uint64_t leaf_size = 0;
    bool large = true;
    if ((*pdpte & kLargePage) != 0) {
      if ((addr & (kSize1G - 1)) == 0 && end - addr >= kSize1G) {
        leaf = pdpte;
        leaf_size = kSize1G;
      } else if (!Split(pdpte, kSize1G, arena)) {
        ok = false;
        break;
      }
    }
    if (leaf == nullptr) {
      uint64_t* pde = &TableAt(*pdpte)[(addr >> 21) & 511];
      if ((*pde & kPresent) == 0) {
        ok = false;
        break;
      }
      if ((*pde & kLargePage) != 0) {
        if ((addr & (kSize2M - 1)) == 0 && end - addr >= kSize2M) {
          leaf = pde;
          leaf_size = kSize2M;
        } else if (!Split(pde, kSize2M, arena)) {
          ok = false;
          break;
        }
      }
      if (leaf == nullptr) {
        leaf = &TableAt(*pde)[(addr >> 12) & 511];
        leaf_size = kSize4K;
        large = false;
      }
    }
    if (count == kMaxEntries) {
      ok = false;
      break;
    }
    const uint64_t cleared = *leaf & ~(kPwt | kPcd | (large ? kPatLarge : kPat4K));
    entries[count++] = {leaf, WithWc(cleared, large)};
    addr += leaf_size;
  }
  WriteCr0(cr0);
  if (!ok) {
    Log("wc: unexpected page tables, leaving the framebuffer as it is\n");
    FlushTlb();  // splits keep the original attributes, so nothing changed
    return false;
  }

  // PAT entry 7 -> WC, then point the entries at it; caches flushed around memory type changes, as the SDM asks.
  WriteCr0(cr0 & ~kCr0WriteProtect);
  Wbinvd();
  Wrmsr(kMsrPat, (pat & ~(0xffull << (8 * kWcPatIndex))) | (kPatTypeWc << (8 * kWcPatIndex)));
  for (int i = 0; i < count; ++i) {
    *entries[i].entry = entries[i].write_combining;
  }
  FlushTlb();
  Wbinvd();
  WriteCr0(cr0);
  Log("wc: %d page table entries for %llu KiB at %llx\n", count, static_cast<unsigned long long>(size >> 10),
      static_cast<unsigned long long>(base));
  return true;
}

}  // namespace x86
