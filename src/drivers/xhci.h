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

// A device on a root port, after AddressDevice.
struct Device {
  int slot = 0;
  int port = 0;
  Speed speed = Speed::kNone;
  uint16_t ep0_max_packet = 8;
  uint8_t* input_context = nullptr;   // input control context + slot + 31 endpoint contexts
  uint8_t* output_context = nullptr;  // device context (owned by the controller once addressed)
  uint8_t* buffer = nullptr;          // 4 KiB for control transfer data
  Ring ep0;
};

// Completion of a transfer on an interrupt (or bulk) endpoint.
struct TransferEvent {
  int slot = 0;
  int endpoint = 0;        // device context index (DCI): endpoint number * 2 + 1 for IN
  int code = 0;            // 1 success, 13 short packet
  uint32_t remaining = 0;  // bytes not transferred
};

// USB standard request types (bmRequestType).
inline constexpr uint8_t kRequestIn = 0x80, kRequestClass = 0x20, kRequestInterface = 0x01;

class Controller {
 public:
  static constexpr int kMaxPorts = 32;
  static constexpr int kCompletionSuccess = 1, kCompletionShortPacket = 13;

  // BIOS handoff, reset, rings, run. Logs what it finds; description() summarises it.
  bool Init(const pci::Device& device, lib::Arena& arena, uint64_t tsc_hz);

  int port_count() const { return ports_; }
  // After the reset, ports need some time to see their devices again: waits until the connection states have been
  // stable for 100 ms (at most `max_ms`). Returns the number of connected ports.
  int WaitForPorts(int max_ms);
  PortStatus Port(int port);         // 1-based, as in the specification
  uint32_t RawPortStatus(int port);  // PORTSC, for diagnostics
  // Resets a USB 2 port with a device (USB 3 ports enable themselves); true if it is enabled afterwards.
  bool ResetPort(int port);

  // Enable Slot + Address Device for the (reset, enabled) device on `port`, then reads the first 8 bytes of the device
  // descriptor to learn endpoint 0's packet size. Allocates the device's memory from `arena`.
  bool AddressDevice(int port, Device* device, lib::Arena& arena);
  // A control transfer on endpoint 0 with device->buffer as data (length <= 4096). Returns the completion code.
  int ControlTransfer(Device& device, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                      uint16_t length);
  // Configure Endpoint for one interrupt IN endpoint (address bit 7 set), with `ring` as its transfer ring.
  // `interval` is bInterval from the endpoint descriptor.
  bool ConfigureInterruptIn(Device& device, uint8_t endpoint_address, uint16_t max_packet, uint8_t interval,
                            Ring& ring);
  // Queues a read of `length` bytes into `buffer` on an endpoint configured above; completes as a TransferEvent.
  void QueueIn(Device& device, int endpoint, Ring& ring, void* buffer, uint32_t length);
  // Next completed transfer on a non-control endpoint, if any.
  bool PollTransfer(TransferEvent* event);

  // Sends a command TRB and waits for its completion. Returns the completion code (1 = success, 0 = timeout) and
  // the event's slot ID.
  int Command(const Trb& trb, uint8_t* slot_id = nullptr);

  // Halts the controller (no more DMA into rings and buffers), before a hot reload.
  void Halt();

  const char* description() const { return description_; }

 private:
  struct Event {
    Trb trb;
    bool valid;
  };
  Event NextEvent();  // one event from the event ring, if any
  // Transfer events for other endpoints that arrive while waiting for a command or control transfer.
  void HandleOtherEvent(const Trb& event);
  uint8_t* Context(uint8_t* base, int index) const { return base + index * (context64_ ? 64 : 32); }
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
  static constexpr int kQueuedTransfers = 16;
  TransferEvent queued_[kQueuedTransfers];
  int queued_count_ = 0;
};

}  // namespace xhci
