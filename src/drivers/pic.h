// Legacy 8259 interrupt controllers: moved to vectors 0x20-0x2F (away from CPU exceptions) and fully masked, since
// the APICs are used instead.
#pragma once

namespace pic {

inline constexpr int kVectorBase = 0x20;

void RemapAndMaskAll();

}  // namespace pic
