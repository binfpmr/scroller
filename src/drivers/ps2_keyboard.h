// PS/2 keyboard on the i8042 controller (ports 0x60/0x64). Only valid after ExitBootServices, when the firmware no
// longer drives the controller. Starts polled; StartInterrupts switches to IRQ 1 (bytes are queued by the interrupt
// handler and decoded in Poll), falling back to polling if no interrupt comes for a waiting byte.
#pragma once

#include "drivers/ps2_decoder.h"
#include "platform/input.h"

namespace ps2 {

class Keyboard final : public platform::KeyboardSource {
 public:
  // Returns false if there is no controller. Turns scan code translation on (set 1) and the controller's IRQs off.
  bool Init();
  void Poll(platform::InputTracker& tracker) override;
  bool StartInterrupts(uint8_t vector, uint64_t tsc_hz) override;
  const char* mode() const override;

 private:
  void Decode(uint8_t byte, platform::InputTracker& tracker);
  void PollPorts(platform::InputTracker& tracker);

  Decoder decoder_;
  bool present_ = false;
  bool interrupts_ = false;
  uint64_t tsc_hz_ = 0;
  uint64_t byte_waiting_since_ = 0;  // TSC when a byte was first seen in the controller without an interrupt
};

}  // namespace ps2
