#include "drivers/ps2_keyboard.h"

#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"

namespace ps2 {
namespace {

constexpr uint16_t kData = 0x60;
constexpr uint16_t kStatus = 0x64;
constexpr uint16_t kCommand = 0x64;

constexpr uint8_t kStatusOutputFull = 0x01;
constexpr uint8_t kStatusInputFull = 0x02;
constexpr uint8_t kStatusMouseData = 0x20;

constexpr uint8_t kConfigPort1Irq = 0x01;
constexpr uint8_t kConfigPort2Irq = 0x02;
constexpr uint8_t kConfigTranslation = 0x40;

constexpr int kTimeout = 100000;

bool WaitInputEmpty() {
  for (int i = 0; i < kTimeout; ++i) {
    if ((x86::Inb(kStatus) & kStatusInputFull) == 0) {
      return true;
    }
    x86::Pause();
  }
  return false;
}

bool WaitOutputFull() {
  for (int i = 0; i < kTimeout; ++i) {
    if ((x86::Inb(kStatus) & kStatusOutputFull) != 0) {
      return true;
    }
    x86::Pause();
  }
  return false;
}

void FlushOutput() {
  for (int i = 0; i < 64 && (x86::Inb(kStatus) & kStatusOutputFull) != 0; ++i) {
    x86::Inb(kData);
  }
}

}  // namespace

bool Keyboard::Init() {
  if (x86::Inb(kStatus) == 0xff) {
    Log("ps2: no controller\n");
    return false;
  }
  FlushOutput();

  if (!WaitInputEmpty()) {
    Log("ps2: controller not responding\n");
    return false;
  }
  x86::Outb(kCommand, 0x20);  // read configuration byte
  if (!WaitOutputFull()) {
    Log("ps2: no configuration byte\n");
    return false;
  }
  const uint8_t config = x86::Inb(kData);
  const uint8_t new_config = static_cast<uint8_t>((config | kConfigTranslation) & ~(kConfigPort1Irq | kConfigPort2Irq));
  if (WaitInputEmpty()) {
    x86::Outb(kCommand, 0x60);  // write configuration byte
    if (WaitInputEmpty()) {
      x86::Outb(kData, new_config);
    }
  }
  if (WaitInputEmpty()) {
    x86::Outb(kCommand, 0xae);  // enable first port
  }
  FlushOutput();
  Log("ps2: config %02x -> %02x\n", config, new_config);
  present_ = true;
  return true;
}

void Keyboard::Poll(platform::InputTracker& tracker) {
  if (!present_) {
    return;
  }
  for (int i = 0; i < 32; ++i) {
    const uint8_t status = x86::Inb(kStatus);
    if ((status & kStatusOutputFull) == 0) {
      return;
    }
    const uint8_t byte = x86::Inb(kData);
    if ((status & kStatusMouseData) != 0) {
      continue;
    }
    ScanEvent event;
    platform::Key key;
    if (decoder_.Feed(byte, &event) && MapKey(event.code, &key)) {
      tracker.OnKey(key, event.down);
    }
  }
}

}  // namespace ps2
