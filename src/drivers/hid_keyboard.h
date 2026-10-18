// HID boot-protocol keyboard reports (pure logic, host-tested): 8 bytes, modifiers in byte 0, up to 6 pressed keys
// (usage IDs from the HID keyboard page) in bytes 2..7. Comparing a report with the previous one gives key events.
#pragma once

#include <stdint.h>

#include "platform/input.h"

namespace hid {

// Maps a keyboard usage ID to a game key; false for keys the game does not use.
bool MapUsage(uint8_t usage, platform::Key* key);

class BootKeyboard {
 public:
  // Feeds one report into `tracker` (key down/up for every change). Reports with the "phantom" state (too many keys
  // pressed: all usages 0x01) are ignored. Returns the last newly pressed usage (for the overlay), or 0.
  uint8_t Feed(const uint8_t (&report)[8], platform::InputTracker& tracker);

 private:
  uint8_t previous_[8] = {};
};

}  // namespace hid
