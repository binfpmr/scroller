// USB host controller (xHCI): takes the controller over from the firmware, resets it and runs it without
// interrupts: the command ring and the event ring are polled. Root ports only (no hubs).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "drivers/pci.h"
#include "lib/arena.h"

namespace xhci {

// Transfer request block: the unit of every ring.
struct Trb {
  uint64_t parameter;
  uint32_t status;
  uint32_t control;  // bit 0 cycle, bits 15:10 type
};
static_assert(sizeof(Trb) == 16);

// Producer ring (command ring, transfer rings): a Link TRB at the end leads back to the start.
class Ring {
 public:
  static constexpr int kSize = 64;  // TRBs, including the Link TRB

  bool Init(lib::Arena& arena);
  // Queues a TRB (cycle bit set here); returns its physical address (completion events refer to it).
  uint64_t Push(const Trb& trb);
  uint64_t address() const { return reinterpret_cast<uint64_t>(trbs_); }

 private:
  volatile Trb* trbs_ = nullptr;
  int enqueue_ = 0;
  uint32_t cycle_ = 1;
};

// Port speeds as in PORTSC.
enum class Speed : uint8_t { kNone = 0, kFull = 1, kLow = 2, kHigh = 3, kSuper = 4, kSuperPlus = 5 };
const char* SpeedName(Speed speed);

struct PortStatus {
  bool connected = false;
  bool enabled = false;
  Speed speed = Speed::kNone;
  uint8_t usb_major = 0;  // 2 or 3, from the Supported Protocol capability
};

class Controller {
 public:
  static constexpr int kMaxPorts = 32;

  // BIOS handoff, reset, rings, run. Logs what it finds; description() summarises it.
  bool Init(const pci::Device& device, lib::Arena& arena, uint64_t tsc_hz);

  int port_count() const { return ports_; }
  PortStatus Port(int port);  // 1-based, as in the specification
  // Resets a USB 2 port with a device (USB 3 ports enable themselves); true if it is enabled afterwards.
  bool ResetPort(int port);

  // Sends a command TRB and waits for its completion. Returns the completion code (1 = success, 0 = timeout) and
  // the event's slot ID.
  int Command(const Trb& trb, uint8_t* slot_id = nullptr);

  const char* description() const { return description_; }

 private:
  struct Event {
    Trb trb;
    bool valid;
  };
  Event NextEvent();  // one event from the event ring, if any
  void HandleOtherEvent(const Trb& event);
  // Polls `done` until it returns true or `microseconds` have passed; returns the last result.
  template <typename Done>
  bool Wait(uint64_t microseconds, Done done) const;

  bool TakeOwnership();
  bool Reset();

  volatile uint8_t* cap_ = nullptr;
  volatile uint8_t* op_ = nullptr;
  volatile uint8_t* runtime_ = nullptr;
  volatile uint32_t* doorbells_ = nullptr;
  uint64_t tsc_hz_ = 0;
  int max_slots_ = 0;
  int ports_ = 0;
  bool context64_ = false;  // 64-byte contexts (HCCPARAMS1.CSZ)
  uint8_t port_major_[kMaxPorts + 1] = {};
  uint64_t* dcbaa_ = nullptr;
  Ring command_ring_;
  volatile Trb* events_ = nullptr;
  int event_dequeue_ = 0;
  uint32_t event_cycle_ = 1;
  char description_[64] = "";
};

}  // namespace xhci
