#include "arch/x86_64/paging.h"

#include "arch/x86_64/msr.h"

namespace x86 {

bool IsMapped(uint64_t address) {
  constexpr uint64_t kPresent = 1ull << 0;
  constexpr uint64_t kLargePage = 1ull << 7;
  constexpr uint64_t kAddressMask = 0x000ffffffffff000ull;
  constexpr uint64_t kCr4La57 = 1ull << 12;
  if ((ReadCr4() & kCr4La57) != 0) {
    return false;  // 5-level paging: not handled
  }
  const uint64_t* table = reinterpret_cast<const uint64_t*>(ReadCr3() & kAddressMask);
  for (int shift = 39; shift >= 12; shift -= 9) {
    const uint64_t entry = table[(address >> shift) & 511];
    if ((entry & kPresent) == 0) {
      return false;
    }
    if (shift == 12 || ((shift == 30 || shift == 21) && (entry & kLargePage) != 0)) {
      return true;
    }
    table = reinterpret_cast<const uint64_t*>(entry & kAddressMask);
  }
  return true;
}

}  // namespace x86
