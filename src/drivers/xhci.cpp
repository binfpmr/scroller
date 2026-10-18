#include "drivers/xhci.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "drivers/mmio.h"
#include "lib/format.h"
#include "lib/mem.h"

namespace xhci {
namespace {

// Capability registers.
constexpr uint32_t kCapLength = 0x00, kHcsParams1 = 0x04, kHcsParams2 = 0x08, kHccParams1 = 0x10,
                   kDbOff = 0x14, kRtsOff = 0x18;
// Operational registers.
constexpr uint32_t kUsbCmd = 0x00, kUsbSts = 0x04, kCrcr = 0x18, kDcbaap = 0x30, kConfig = 0x38, kPortBase = 0x400;
constexpr uint32_t kCmdRun = 1u << 0, kCmdReset = 1u << 1;
constexpr uint32_t kStsHalted = 1u << 0, kStsNotReady = 1u << 11;
// PORTSC.
constexpr uint32_t kPortConnected = 1u << 0, kPortEnabled = 1u << 1, kPortReset = 1u << 4, kPortPower = 1u << 9;
constexpr uint32_t kPortChangeBits = 0x7fu << 17;  // CSC PEC WRC OCC PRC PLC CEC, write 1 to clear
constexpr uint32_t kPortResetChange = 1u << 21;
// Interrupter 0 (runtime registers + 0x20).
constexpr uint32_t kIman = 0x20, kErstSz = 0x28, kErstBa = 0x30, kErdp = 0x38;
constexpr uint64_t kErdpBusy = 1u << 3;
// Extended capabilities.
constexpr uint32_t kExtLegacy = 1, kExtProtocol = 2;
constexpr uint32_t kLegacyBiosOwned = 1u << 16, kLegacyOsOwned = 1u << 24;

// TRB types.
constexpr uint32_t kTrbLink = 6, kTrbNoOp = 23, kTrbCommandCompletion = 33, kTrbPortStatusChange = 34;
constexpr uint32_t kLinkToggleCycle = 1u << 1;

constexpr int kEventRingSize = 64;

uint32_t TrbType(const Trb& trb) { return (trb.control >> 10) & 0x3f; }

uint32_t Read32(volatile uint8_t* base, uint32_t offset) { return mmio::Read<uint32_t>(base, offset); }
void Write32(volatile uint8_t* base, uint32_t offset, uint32_t v) { mmio::Write<uint32_t>(base, offset, v); }
void Write64(volatile uint8_t* base, uint32_t offset, uint64_t v) {
  // Two 32-bit writes, low first: controllers without 64-bit register access still accept that.
  Write32(base, offset, static_cast<uint32_t>(v));
  Write32(base, offset + 4, static_cast<uint32_t>(v >> 32));
}

}  // namespace

const char* SpeedName(Speed speed) {
  switch (speed) {
    case Speed::kFull:
      return "FULL";
    case Speed::kLow:
      return "LOW";
    case Speed::kHigh:
      return "HIGH";
    case Speed::kSuper:
      return "SUPER";
    case Speed::kSuperPlus:
      return "SUPER+";
    default:
      return "?";
  }
}

bool Ring::Init(lib::Arena& arena) {
  // 4 KiB aligned, so the ring never crosses a 64 KiB boundary.
  trbs_ = static_cast<volatile Trb*>(arena.Allocate(sizeof(Trb) * kSize, 4096));
  if (trbs_ == nullptr) {
    return false;
  }
  lib::Fill(const_cast<Trb*>(trbs_), 0, sizeof(Trb) * kSize);
  volatile Trb& link = trbs_[kSize - 1];
  link.parameter = address();
  link.status = 0;
  link.control = kTrbLink << 10 | kLinkToggleCycle;  // cycle bit 0: not valid until the ring wraps
  enqueue_ = 0;
  cycle_ = 1;
  return true;
}

uint64_t Ring::Push(const Trb& trb) {
  volatile Trb& slot = trbs_[enqueue_];
  slot.parameter = trb.parameter;
  slot.status = trb.status;
  asm volatile("" ::: "memory");
  slot.control = (trb.control & ~1u) | cycle_;  // the cycle bit last: it hands the TRB to the controller
  const uint64_t at = address() + static_cast<uint64_t>(enqueue_) * sizeof(Trb);
  if (++enqueue_ == kSize - 1) {
    // Hand over the Link TRB with the current cycle, then continue at the start with the other cycle.
    volatile Trb& link = trbs_[kSize - 1];
    link.control = (link.control & ~1u) | cycle_;
    enqueue_ = 0;
    cycle_ ^= 1;
  }
  return at;
}

template <typename Done>
bool Controller::Wait(uint64_t microseconds, Done done) const {
  const uint64_t end = x86::Rdtsc() + microseconds * (tsc_hz_ / 1000000);
  for (;;) {
    if (done()) {
      return true;
    }
    if (x86::Rdtsc() > end) {
      return done();
    }
    x86::Pause();
  }
}

bool Controller::TakeOwnership() {
  // Walk the extended capabilities: USB Legacy Support (firmware ownership), Supported Protocol (which ports are
  // USB 2 and which USB 3).
  uint32_t offset = (Read32(cap_, kHccParams1) >> 16) << 2;
  for (int guard = 0; offset != 0 && guard < 64; ++guard) {
    const uint32_t header = Read32(cap_, offset);
    const uint32_t id = header & 0xff;
    if (id == kExtLegacy) {
      if ((header & kLegacyBiosOwned) != 0) {
        Write32(cap_, offset, header | kLegacyOsOwned);
        const bool released = Wait(1000000, [&] { return (Read32(cap_, offset) & kLegacyBiosOwned) == 0; });
        Log("xhci: firmware handoff %s\n", released ? "done" : "timed out (taking over anyway)");
        Write32(cap_, offset, (Read32(cap_, offset) & ~kLegacyBiosOwned) | kLegacyOsOwned);
      }
      // No more SMIs from USB events: clear the enables, acknowledge pending ones (bits 31:29, write 1 to clear).
      const uint32_t control = Read32(cap_, offset + 4);
      Write32(cap_, offset + 4, (control & 0xe0000000u) & ~0x0000e011u);
    } else if (id == kExtProtocol) {
      const uint8_t major = static_cast<uint8_t>(header >> 24);
      const uint32_t ports = Read32(cap_, offset + 8);
      const int first = static_cast<int>(ports & 0xff);
      const int count = static_cast<int>((ports >> 8) & 0xff);
      for (int p = first; p < first + count && p <= kMaxPorts; ++p) {
        port_major_[p] = major;
      }
    }
    const uint32_t next = (header >> 8) & 0xff;
    offset = next == 0 ? 0 : offset + (next << 2);
  }
  return true;
}

bool Controller::Reset() {
  Write32(op_, kUsbCmd, Read32(op_, kUsbCmd) & ~kCmdRun);
  if (!Wait(100000, [&] { return (Read32(op_, kUsbSts) & kStsHalted) != 0; })) {
    Log("xhci: does not halt\n");
    return false;
  }
  Write32(op_, kUsbCmd, kCmdReset);
  const bool done = Wait(
      1000000, [&] { return (Read32(op_, kUsbCmd) & kCmdReset) == 0 && (Read32(op_, kUsbSts) & kStsNotReady) == 0; });
  if (!done) {
    Log("xhci: reset timed out\n");
  }
  return done;
}

bool Controller::Init(const pci::Device& device, lib::Arena& arena, uint64_t tsc_hz) {
  tsc_hz_ = tsc_hz;
  const uint64_t bar = pci::MemoryBar(device, 0);
  if (bar == 0 || (!x86::IsMapped(bar) && !x86::MapUncached(arena, bar, 0x10000))) {
    Log("xhci: BAR0 unusable\n");
    return false;
  }
  pci::EnableDevice(device);
  cap_ = reinterpret_cast<volatile uint8_t*>(bar);
  op_ = cap_ + (Read32(cap_, kCapLength) & 0xff);
  runtime_ = cap_ + (Read32(cap_, kRtsOff) & ~0x1fu);
  doorbells_ = reinterpret_cast<volatile uint32_t*>(cap_ + (Read32(cap_, kDbOff) & ~0x3u));
  const uint32_t params1 = Read32(cap_, kHcsParams1);
  const uint32_t params2 = Read32(cap_, kHcsParams2);
  max_slots_ = static_cast<int>(params1 & 0xff);
  ports_ = lib::Min(static_cast<int>(params1 >> 24), kMaxPorts);
  context64_ = (Read32(cap_, kHccParams1) & (1u << 2)) != 0;
  const int scratchpads = static_cast<int>(((params2 >> 21) & 0x1f) << 5 | ((params2 >> 27) & 0x1f));

  TakeOwnership();
  if (!Reset()) {
    return false;
  }

  // Device context base address array (entry 0: scratchpad buffer array, if the controller wants one).
  const int slots = lib::Min(max_slots_, 32);
  dcbaa_ = static_cast<uint64_t*>(arena.Allocate(sizeof(uint64_t) * (slots + 1), 64));
  if (dcbaa_ == nullptr) {
    return false;
  }
  lib::Fill(dcbaa_, 0, sizeof(uint64_t) * (slots + 1));
  if (scratchpads > 0) {
    auto* array = static_cast<uint64_t*>(arena.Allocate(sizeof(uint64_t) * scratchpads, 64));
    if (array == nullptr) {
      return false;
    }
    for (int i = 0; i < scratchpads; ++i) {
      void* page = arena.Allocate(4096, 4096);  // PAGESIZE is 4 KiB on every controller seen so far
      if (page == nullptr) {
        return false;
      }
      lib::Fill(page, 0, 4096);
      array[i] = reinterpret_cast<uint64_t>(page);
    }
    dcbaa_[0] = reinterpret_cast<uint64_t>(array);
  }
  Write32(op_, kConfig, static_cast<uint32_t>(slots));
  Write64(op_, kDcbaap, reinterpret_cast<uint64_t>(dcbaa_));

  if (!command_ring_.Init(arena)) {
    return false;
  }
  Write64(op_, kCrcr, command_ring_.address() | 1);  // ring cycle state 1

  // Event ring: one segment, described by a one-entry segment table.
  events_ = static_cast<volatile Trb*>(arena.Allocate(sizeof(Trb) * kEventRingSize, 4096));
  auto* erst = static_cast<uint64_t*>(arena.Allocate(16, 64));
  if (events_ == nullptr || erst == nullptr) {
    return false;
  }
  lib::Fill(const_cast<Trb*>(events_), 0, sizeof(Trb) * kEventRingSize);
  erst[0] = reinterpret_cast<uint64_t>(events_);
  erst[1] = kEventRingSize;
  event_dequeue_ = 0;
  event_cycle_ = 1;
  Write32(runtime_, kErstSz, 1);
  Write64(runtime_, kErdp, reinterpret_cast<uint64_t>(events_));
  Write64(runtime_, kErstBa, reinterpret_cast<uint64_t>(erst));
  Write32(runtime_, kIman, 0);  // polled: interrupter disabled

  Write32(op_, kUsbCmd, kCmdRun);
  if (!Wait(100000, [&] { return (Read32(op_, kUsbSts) & kStsHalted) == 0; })) {
    Log("xhci: does not start\n");
    return false;
  }

  // Ports are powered by the reset on most controllers; switch on any that are not.
  for (int p = 1; p <= ports_; ++p) {
    const uint32_t portsc = Read32(op_, kPortBase + 0x10 * (p - 1));
    if ((portsc & kPortPower) == 0) {
      Write32(op_, kPortBase + 0x10 * (p - 1), (portsc & ~(kPortChangeBits | kPortEnabled)) | kPortPower);
    }
  }

  const int noop = Command({.parameter = 0, .status = 0, .control = kTrbNoOp << 10});
  const uint32_t version = Read32(cap_, kCapLength) >> 16;  // HCIVERSION; some controllers want 32-bit reads
  Log("xhci: %04x:%04x version %x.%02x, %d slots, %d ports, %d scratchpads, %d-byte contexts, no-op command %s\n",
      device.vendor_id, device.device_id, version >> 8, version & 0xff, max_slots_, ports_, scratchpads,
      context64_ ? 64 : 32, noop == 1 ? "ok" : "FAILED");
  lib::Format(description_, sizeof(description_), "XHCI %04x:%04x %d PORTS", device.vendor_id, device.device_id,
              ports_);
  return noop == 1;
}

PortStatus Controller::Port(int port) {
  const uint32_t portsc = Read32(op_, kPortBase + 0x10 * (port - 1));
  return {.connected = (portsc & kPortConnected) != 0,
          .enabled = (portsc & kPortEnabled) != 0,
          .speed = static_cast<Speed>((portsc >> 10) & 0xf),
          .usb_major = port_major_[port]};
}

bool Controller::ResetPort(int port) {
  const uint32_t offset = kPortBase + 0x10 * (port - 1);
  const uint32_t portsc = Read32(op_, offset);
  if ((portsc & kPortConnected) == 0) {
    return false;
  }
  if ((portsc & kPortEnabled) != 0 && port_major_[port] >= 3) {
    return true;  // USB 3 ports train and enable on their own
  }
  // Writing PED or a change bit as 1 would disable the port or clear the change: keep them 0.
  Write32(op_, offset, (portsc & ~(kPortChangeBits | kPortEnabled)) | kPortReset);
  Wait(200000, [&] { return (Read32(op_, offset) & kPortResetChange) != 0; });
  const uint32_t after = Read32(op_, offset);
  Write32(op_, offset, (after & ~(kPortChangeBits | kPortEnabled)) | (after & kPortChangeBits));  // acknowledge
  return (after & kPortEnabled) != 0;
}

Controller::Event Controller::NextEvent() {
  const volatile Trb& slot = events_[event_dequeue_];
  if ((slot.control & 1) != event_cycle_) {
    return {.trb = {}, .valid = false};
  }
  asm volatile("" ::: "memory");
  const Trb event = {.parameter = slot.parameter, .status = slot.status, .control = slot.control};
  if (++event_dequeue_ == kEventRingSize) {
    event_dequeue_ = 0;
    event_cycle_ ^= 1;
  }
  Write64(runtime_, kErdp, reinterpret_cast<uint64_t>(&events_[event_dequeue_]) | kErdpBusy);
  return {.trb = event, .valid = true};
}

void Controller::HandleOtherEvent(const Trb& event) {
  if (TrbType(event) == kTrbPortStatusChange) {
    return;  // ports are looked at directly
  }
  Log("xhci: unexpected event type %u, code %u\n", TrbType(event), event.status >> 24);
}

int Controller::Command(const Trb& trb, uint8_t* slot_id) {
  const uint64_t at = command_ring_.Push(trb);
  doorbells_[0] = 0;
  int code = 0;
  Wait(500000, [&] {
    for (Event e = NextEvent(); e.valid; e = NextEvent()) {
      if (TrbType(e.trb) == kTrbCommandCompletion && e.trb.parameter == at) {
        code = static_cast<int>(e.trb.status >> 24);
        if (slot_id != nullptr) {
          *slot_id = static_cast<uint8_t>(e.trb.control >> 24);
        }
        return true;
      }
      HandleOtherEvent(e.trb);
    }
    return false;
  });
  return code;
}

}  // namespace xhci
