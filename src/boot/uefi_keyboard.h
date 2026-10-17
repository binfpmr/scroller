// Fallback input for machines whose keyboard we cannot drive yet (USB only): reads UEFI console input while boot
// services stay active (SCROLLER_UEFI_INPUT build). UEFI reports key presses and typematic repeats but no releases,
// so a key counts as held until a timeout passes without another repeat.
#pragma once

#include <stdint.h>

#include "platform/input.h"
#include "uefi/uefi.h"

namespace boot {

class UefiKeyboard final : public platform::KeyboardSource {
 public:
  UefiKeyboard(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* input, uint64_t tsc_hz);
  void Poll(platform::InputTracker& tracker) override;

 private:
  static constexpr int kKeys = static_cast<int>(platform::Key::kCount);

  EFI_SIMPLE_TEXT_INPUT_PROTOCOL* input_;
  uint64_t first_hold_cycles_;   // covers the typematic delay before repeats start
  uint64_t repeat_hold_cycles_;  // covers the gap between repeats
  uint64_t release_at_[kKeys] = {};
  bool down_[kKeys] = {};
};

}  // namespace boot
