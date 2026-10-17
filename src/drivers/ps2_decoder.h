// Decodes PS/2 scan code set 1 (what the i8042 delivers with translation on) into key events. Pure logic, no I/O.
#pragma once

#include <stdint.h>

#include "platform/input.h"

namespace ps2 {

struct ScanEvent {
  uint16_t code = 0;  // make code; 0xE0xx for extended keys
  bool down = false;
};

class Decoder {
 public:
  // Returns true and fills `event` when `byte` completes a key event.
  bool Feed(uint8_t byte, ScanEvent* event);

 private:
  bool extended_ = false;
  int skip_ = 0;  // bytes left of a Pause sequence (E1 1D 45 E1 9D C5)
};

// Maps a scan code to a game key; returns false for keys the game does not use.
bool MapKey(uint16_t code, platform::Key* key);

}  // namespace ps2
