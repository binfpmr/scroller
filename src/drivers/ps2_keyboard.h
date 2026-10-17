// Polled PS/2 keyboard on the i8042 controller (ports 0x60/0x64). Only valid after ExitBootServices, when the
// firmware no longer drives the controller.
#pragma once

#include "drivers/ps2_decoder.h"
#include "platform/input.h"

namespace ps2 {

class Keyboard final : public platform::KeyboardSource {
 public:
  // Returns false if there is no controller. Turns scan code translation on (set 1) and the controller's IRQs off.
  bool Init();
  void Poll(platform::InputTracker& tracker) override;

 private:
  Decoder decoder_;
  bool present_ = false;
};

}  // namespace ps2
