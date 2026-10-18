#include "drivers/hid_keyboard.h"

namespace hid {
namespace {

using platform::Key;

constexpr uint8_t kModLeftCtrl = 0x01, kModLeftAlt = 0x04, kModRightCtrl = 0x10, kModRightAlt = 0x40;
constexpr uint8_t kPhantom = 0x01;

bool Contains(const uint8_t (&report)[8], uint8_t usage) {
  for (int i = 2; i < 8; ++i) {
    if (report[i] == usage) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool MapUsage(uint8_t usage, Key* key) {
  switch (usage) {
    case 0x50:  // left arrow
    case 0x5c:  // keypad 4
      *key = Key::kLeft;
      return true;
    case 0x4f:  // right arrow
    case 0x5e:  // keypad 6
      *key = Key::kRight;
      return true;
    case 0x51:  // down arrow
    case 0x5a:  // keypad 2
      *key = Key::kDown;
      return true;
    case 0x52:  // up arrow
    case 0x60:  // keypad 8
    case 0x2c:  // space
      *key = Key::kJump;
      return true;
    case 0x29:
      *key = Key::kEscape;
      return true;
    case 0x3a:
      *key = Key::kToggleStats;  // F1
      return true;
    case 0x3b:
      *key = Key::kNextLevel;  // F2
      return true;
    case 0x2e:  // '=' / '+'
    case 0x80:  // volume up
      *key = Key::kVolumeUp;
      return true;
    case 0x2d:  // '-'
    case 0x81:  // volume down
      *key = Key::kVolumeDown;
      return true;
    case 0x10:  // 'M'
    case 0x7f:  // mute
      *key = Key::kVolumeMute;
      return true;
    case 0x15:  // 'R'
      *key = Key::kReload;
      return true;
    case 0x11:  // 'N'
      *key = Key::kToggleMusic;
      return true;
    case 0x4c:  // Delete
    case 0x63:  // keypad Del
      *key = Key::kDelete;
      return true;
    default:
      return false;
  }
}

uint8_t BootKeyboard::Feed(const uint8_t (&report)[8], platform::InputTracker& tracker) {
  if (report[2] == kPhantom) {
    return 0;
  }
  // Modifiers: Ctrl and Alt, either side.
  const auto modifier = [&](uint8_t bits, Key key) {
    const bool was = (previous_[0] & bits) != 0;
    const bool is = (report[0] & bits) != 0;
    if (was != is) {
      tracker.OnKey(key, is);
    }
  };
  modifier(kModLeftCtrl | kModRightCtrl, Key::kCtrl);
  modifier(kModLeftAlt | kModRightAlt, Key::kAlt);

  Key key;
  for (int i = 2; i < 8; ++i) {
    const uint8_t usage = previous_[i];
    if (usage > kPhantom && !Contains(report, usage) && MapUsage(usage, &key)) {
      tracker.OnKey(key, false);
    }
  }
  uint8_t newest = 0;
  for (int i = 2; i < 8; ++i) {
    const uint8_t usage = report[i];
    if (usage > kPhantom && !Contains(previous_, usage)) {
      newest = usage;
      if (MapUsage(usage, &key)) {
        tracker.OnKey(key, true);
      }
    }
  }
  for (int i = 0; i < 8; ++i) {
    previous_[i] = report[i];
  }
  return newest;
}

}  // namespace hid
