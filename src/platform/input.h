// Game-facing keyboard state. Drivers report key changes to an InputTracker; the game loop takes one InputState per
// fixed update tick.
#pragma once

#include <stdint.h>

namespace platform {

enum class Key : uint8_t {
  kLeft,
  kRight,
  kDown,
  kJump,  // Space or Up
  kEscape,
  kToggleStats,          // F1: performance overlay
  kDebugCrashOpcode,     // F9 (Debug builds): invalid opcode, to test crash reports
  kDebugCrashPageFault,  // F10 (Debug builds): page fault
  kCount,
};

struct InputState {
  uint32_t held = 0;
  uint32_t pressed = 0;   // went down since the previous tick (also set for a tap that is already released again)
  uint32_t released = 0;  // went up since the previous tick

  static constexpr uint32_t Bit(Key key) { return 1u << static_cast<uint32_t>(key); }
  bool Held(Key key) const { return (held & Bit(key)) != 0; }
  bool Pressed(Key key) const { return (pressed & Bit(key)) != 0; }
  bool Released(Key key) const { return (released & Bit(key)) != 0; }
};

// Two physical keys mapped to the same Key (Space and Up) share one state: releasing either releases the Key.
class InputTracker {
 public:
  void OnKey(Key key, bool down);

  // State for one update tick; clears the pressed/released edges.
  InputState NextTick();

 private:
  uint32_t held_ = 0;
  uint32_t pressed_ = 0;
  uint32_t released_ = 0;
};

// Source of key events (PS/2 driver, UEFI console input, ...), polled by the game loop.
class KeyboardSource {
 public:
  virtual void Poll(InputTracker& tracker) = 0;
  // Called once interrupt controllers are running; a source that can, switches to interrupt-driven input on
  // `vector`. Returns true if it did.
  virtual bool StartInterrupts(uint8_t /*vector*/, uint64_t /*tsc_hz*/) { return false; }
  // Short description for the stats overlay, e.g. "PS/2 IRQ".
  virtual const char* mode() const = 0;

 protected:
  ~KeyboardSource() = default;
};

}  // namespace platform
