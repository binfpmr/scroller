#include "platform/input.h"

namespace platform {

void InputTracker::OnKey(Key key, bool down) {
  const uint32_t bit = InputState::Bit(key);
  if (down) {
    if ((held_ & bit) == 0) {
      pressed_ |= bit;  // auto-repeat of a held key is not a new press
    }
    held_ |= bit;
  } else if ((held_ & bit) != 0) {
    held_ &= ~bit;
    released_ |= bit;
  }
}

InputState InputTracker::NextTick() {
  InputState state{.held = held_, .pressed = pressed_, .released = released_};
  pressed_ = 0;
  released_ = 0;
  return state;
}

}  // namespace platform
