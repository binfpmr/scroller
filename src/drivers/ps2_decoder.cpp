#include "drivers/ps2_decoder.h"

namespace ps2 {

bool Decoder::Feed(uint8_t byte, ScanEvent* event) {
  if (skip_ > 0) {
    --skip_;
    return false;
  }
  if (byte == 0xe0) {
    extended_ = true;
    return false;
  }
  if (byte == 0xe1) {
    skip_ = 2;
    return false;
  }
  if (byte == 0x00 || byte == 0xfa || byte == 0xfe || byte == 0xff) {
    extended_ = false;  // error, ACK, resend, overrun: not key data
    return false;
  }
  event->code = static_cast<uint16_t>((extended_ ? 0xe000 : 0) | (byte & 0x7f));
  event->down = (byte & 0x80) == 0;
  extended_ = false;
  return true;
}

bool MapKey(uint16_t code, platform::Key* key) {
  using platform::Key;
  switch (code) {
    case 0xe04b:  // left arrow
    case 0x004b:  // keypad 4
      *key = Key::kLeft;
      return true;
    case 0xe04d:
    case 0x004d:
      *key = Key::kRight;
      return true;
    case 0xe050:
    case 0x0050:
      *key = Key::kDown;
      return true;
    case 0x0039:  // space
    case 0xe048:  // up arrow
    case 0x0048:  // keypad 8
      *key = Key::kJump;
      return true;
    case 0x0001:
      *key = Key::kEscape;
      return true;
    case 0xe030:  // volume up (media key)
    case 0x000d:  // '=' / '+'
      *key = Key::kVolumeUp;
      return true;
    case 0xe02e:  // volume down
    case 0x000c:  // '-'
      *key = Key::kVolumeDown;
      return true;
    case 0xe020:  // mute
    case 0x0032:  // M
      *key = Key::kVolumeMute;
      return true;
    case 0x003b:  // F1
      *key = Key::kToggleStats;
      return true;
    case 0x0031:  // N
      *key = Key::kToggleMusic;
      return true;
    case 0x001d:  // left Ctrl
    case 0xe01d:  // right Ctrl
      *key = Key::kCtrl;
      return true;
    case 0x0038:  // left Alt
    case 0xe038:  // right Alt (AltGr)
      *key = Key::kAlt;
      return true;
    case 0xe053:  // Delete
    case 0x0053:  // keypad Del
      *key = Key::kDelete;
      return true;
    default:
      return false;
  }
}

}  // namespace ps2
