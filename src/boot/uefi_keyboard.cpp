#include "boot/uefi_keyboard.h"

#include "arch/x86_64/cpu.h"

namespace boot {
namespace {

bool MapKey(const EFI_INPUT_KEY& key, platform::Key* out) {
  using platform::Key;
  switch (key.ScanCode) {
    case SCAN_LEFT:
      *out = Key::kLeft;
      return true;
    case SCAN_RIGHT:
      *out = Key::kRight;
      return true;
    case SCAN_UP:
      *out = Key::kJump;
      return true;
    case SCAN_DOWN:
      *out = Key::kDown;
      return true;
    case SCAN_ESC:
      *out = Key::kEscape;
      return true;
    case SCAN_F1:
      *out = Key::kToggleStats;
      return true;
    case SCAN_F2:
      *out = Key::kToggleWriteCombining;
      return true;
    default:
      break;
  }
  if (key.UnicodeChar == u' ') {
    *out = Key::kJump;
    return true;
  }
  return false;
}

}  // namespace

UefiKeyboard::UefiKeyboard(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* input, uint64_t tsc_hz)
    : input_(input), first_hold_cycles_(tsc_hz * 55 / 100), repeat_hold_cycles_(tsc_hz * 15 / 100) {}

void UefiKeyboard::Poll(platform::InputTracker& tracker) {
  const uint64_t now = x86::Rdtsc();
  EFI_INPUT_KEY key;
  while (!EFI_ERROR(input_->ReadKeyStroke(input_, &key))) {
    platform::Key mapped;
    if (!MapKey(key, &mapped)) {
      continue;
    }
    const int i = static_cast<int>(mapped);
    if (!down_[i]) {
      down_[i] = true;
      release_at_[i] = now + first_hold_cycles_;
      tracker.OnKey(mapped, true);
    } else {
      release_at_[i] = now + repeat_hold_cycles_;
    }
  }
  for (int i = 0; i < kKeys; ++i) {
    if (down_[i] && now >= release_at_[i]) {
      down_[i] = false;
      tracker.OnKey(static_cast<platform::Key>(i), false);
    }
  }
}

}  // namespace boot
