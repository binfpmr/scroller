#include "drivers/ps2_keyboard.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"
#include "drivers/local_apic.h"
#include "lib/ring_buffer.h"
#include "platform/interrupt_controllers.h"

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

// Filled by the interrupt handler, drained by Poll.
lib::RingBuffer<256> g_bytes;

void OnInterrupt(x86::InterruptFrame&) {
  for (int i = 0; i < 16; ++i) {
    const uint8_t status = x86::Inb(kStatus);
    if ((status & kStatusOutputFull) == 0) {
      break;
    }
    const uint8_t byte = x86::Inb(kData);
    if ((status & kStatusMouseData) == 0) {
      g_bytes.Push(byte);
    }
  }
  lapic::EndOfInterrupt();
}

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

void Keyboard::Decode(uint8_t byte, platform::InputTracker& tracker) {
  ScanEvent event;
  platform::Key key;
  if (decoder_.Feed(byte, &event) && MapKey(event.code, &key)) {
    tracker.OnKey(key, event.down);
  }
}

void Keyboard::PollPorts(platform::InputTracker& tracker) {
  for (int i = 0; i < 32; ++i) {
    const uint8_t status = x86::Inb(kStatus);
    if ((status & kStatusOutputFull) == 0) {
      return;
    }
    const uint8_t byte = x86::Inb(kData);
    if ((status & kStatusMouseData) == 0) {
      Decode(byte, tracker);
    }
  }
}

void Keyboard::Poll(platform::InputTracker& tracker) {
  if (!present_) {
    return;
  }
  if (!interrupts_) {
    PollPorts(tracker);
    return;
  }
  uint8_t byte;
  while (g_bytes.Pop(&byte)) {
    Decode(byte, tracker);
  }
  // A byte left waiting in the controller means its interrupt is not arriving: go back to polling.
  if ((x86::Inb(kStatus) & kStatusOutputFull) == 0) {
    byte_waiting_since_ = 0;
  } else if (byte_waiting_since_ == 0) {
    byte_waiting_since_ = x86::Rdtsc();
  } else if (x86::Rdtsc() - byte_waiting_since_ > tsc_hz_ / 20) {
    Log("ps2: no interrupt for a waiting byte, back to polling\n");
    platform::MaskIsaIrq(1);
    interrupts_ = false;
    PollPorts(tracker);
  }
}

bool Keyboard::StartInterrupts(uint8_t vector, uint64_t tsc_hz) {
  if (!present_) {
    return false;
  }
  tsc_hz_ = tsc_hz;
  x86::DisableInterrupts();
  // Enable the controller's IRQ 1 (the configuration byte is read through the data port, so no handler may run).
  bool ok = WaitInputEmpty();
  uint8_t config = 0;
  if (ok) {
    x86::Outb(kCommand, 0x20);
    ok = WaitOutputFull();
  }
  if (ok) {
    config = x86::Inb(kData);
    ok = WaitInputEmpty();
  }
  if (ok) {
    x86::Outb(kCommand, 0x60);
    ok = WaitInputEmpty();
  }
  if (ok) {
    x86::Outb(kData, static_cast<uint8_t>(config | kConfigPort1Irq));
  }
  // The IRQ is edge triggered: a byte already waiting would never raise one, so drop it.
  FlushOutput();
  ok = ok && platform::RouteIsaIrq(1, vector, &OnInterrupt);
  if (!ok) {
    Log("ps2: interrupts not available, polling\n");
  }
  interrupts_ = ok;
  x86::EnableInterrupts();
  return ok;
}

const char* Keyboard::mode() const {
  if (!present_) {
    return "NONE";
  }
  return interrupts_ ? "PS/2 IRQ" : "PS/2 POLL";
}

}  // namespace ps2
