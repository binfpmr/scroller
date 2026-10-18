#include "platform/usb.h"

#include "arch/x86_64/cpu.h"
#include "drivers/debug_log.h"
#include "drivers/hid_keyboard.h"
#include "drivers/usb_descriptors.h"
#include "drivers/xhci.h"
#include "lib/format.h"
#include "lib/mem.h"

namespace platform {
namespace {

constexpr uint32_t kClassXhci = 0x0c0330;  // serial bus, USB, xHCI
constexpr int kMaxDevices = 8;
constexpr uint64_t kConnectSettleMs = 100;  // USB 2.0 7.1.7.3: wait after a connect before resetting the port

// USB requests.
constexpr uint8_t kGetDescriptor = 6, kSetConfiguration = 9, kHidSetIdle = 0x0a, kHidSetProtocol = 0x0b;

struct UsbDevice {
  bool present = false;  // entries of unplugged devices are reused, with their memory
  xhci::Device device;
  usb::DeviceInfo info;
  bool keyboard = false;
  int keyboard_endpoint = 0;  // DCI
  xhci::Ring keyboard_ring;
  uint8_t* report = nullptr;  // 8 bytes, DMA target
};

constinit xhci::Controller g_xhci;
bool g_have_xhci = false;
constinit UsbDevice g_devices[kMaxDevices];
hid::BootKeyboard g_keyboard_state[kMaxDevices];
uint8_t g_last_usage = 0;
char g_status[96] = "NONE";
lib::Arena* g_arena = nullptr;
uint64_t g_tsc_hz = 0;
uint32_t g_waiting_ports = 0;  // connected, enumerated once they have settled
uint64_t g_waiting_since_ms[xhci::Controller::kMaxPorts + 1] = {};
volatile bool g_busy = false;

uint64_t NowMs() { return x86::Rdtsc() / (g_tsc_hz / 1000); }

void UpdateStatus() {
  int devices = 0, keyboards = 0;
  for (const UsbDevice& d : g_devices) {
    devices += d.present ? 1 : 0;
    keyboards += d.present && d.keyboard ? 1 : 0;
  }
  lib::Format(g_status, sizeof(g_status), "%s  %d DEV  %d KBD", g_xhci.description(), devices, keyboards);
}

UsbDevice* DeviceOnPort(int port) {
  for (UsbDevice& d : g_devices) {
    if (d.present && d.device.port == port) {
      return &d;
    }
  }
  return nullptr;
}

// Reads the configuration and, for a HID boot keyboard, sets it up and queues the first report read.
void SetUp(UsbDevice& d, lib::Arena& arena) {
  xhci::Device& dev = d.device;
  int code = g_xhci.ControlTransfer(dev, xhci::kRequestIn, kGetDescriptor, 0x0100, 0, 18);
  if (code != xhci::Controller::kCompletionSuccess || !usb::ParseDevice(dev.buffer, 18, &d.info)) {
    Log("usb: port %d: no device descriptor (code %d)\n", dev.port, code);
    return;
  }
  usb::Configuration config;
  code = g_xhci.ControlTransfer(dev, xhci::kRequestIn, kGetDescriptor, 0x0200, 0, 9);
  if (code != xhci::Controller::kCompletionSuccess || !usb::ParseConfiguration(dev.buffer, 9, &config)) {
    Log("usb: port %d: no configuration descriptor (code %d)\n", dev.port, code);
    return;
  }
  const uint16_t total = lib::Min<uint16_t>(config.total_length, 4096);
  code = g_xhci.ControlTransfer(dev, xhci::kRequestIn, kGetDescriptor, 0x0200, 0, total);
  if ((code != xhci::Controller::kCompletionSuccess && code != xhci::Controller::kCompletionShortPacket) ||
      !usb::ParseConfiguration(dev.buffer, total, &config)) {
    Log("usb: port %d: configuration descriptor failed (code %d)\n", dev.port, code);
    return;
  }
  Log("usb: port %d: %04x:%04x USB %x.%02x, %s speed, %d interface(s):", dev.port, d.info.vendor_id, d.info.product_id,
      d.info.usb_version >> 8, d.info.usb_version & 0xff, xhci::SpeedName(dev.speed), config.interface_count);
  for (int i = 0; i < config.interface_count; ++i) {
    const usb::Interface& in = config.interfaces[i];
    Log(" %02x/%02x/%02x", in.interface_class, in.subclass, in.protocol);
  }
  Log("\n");

  const usb::Interface* interface = nullptr;
  const usb::Endpoint* ep = usb::FindBootKeyboard(config, &interface);
  if (ep == nullptr) {
    return;
  }
  if (d.report == nullptr) {
    d.report = static_cast<uint8_t*>(arena.Allocate(64, 64));
  }
  if (d.report == nullptr || !d.keyboard_ring.Init(arena) ||
      !g_xhci.ConfigureInterruptIn(dev, ep->address, ep->max_packet, ep->interval, d.keyboard_ring)) {
    return;
  }
  code = g_xhci.ControlTransfer(dev, 0, kSetConfiguration, config.value, 0, 0);
  if (code != xhci::Controller::kCompletionSuccess) {
    Log("usb: port %d: SET_CONFIGURATION failed (code %d)\n", dev.port, code);
    return;
  }
  const uint8_t class_interface = xhci::kRequestClass | xhci::kRequestInterface;
  g_xhci.ControlTransfer(dev, class_interface, kHidSetProtocol, 0, interface->number, 0);  // 0: boot protocol
  g_xhci.ControlTransfer(dev, class_interface, kHidSetIdle, 0, interface->number, 0);      // reports on change only
  d.keyboard = true;
  d.keyboard_endpoint = (ep->address & 0xf) * 2 + 1;
  lib::Fill(d.report, 0, 8);
  g_xhci.QueueIn(dev, d.keyboard_endpoint, d.keyboard_ring, d.report, 8);
  Log("usb: port %d: boot keyboard, endpoint %02x, %u bytes every %u\n", dev.port, ep->address, ep->max_packet,
      ep->interval);
}

// Resets the port, addresses the device in a free entry and sets it up.
void Enumerate(int port) {
  UsbDevice* d = nullptr;
  for (UsbDevice& candidate : g_devices) {
    if (!candidate.present) {
      d = &candidate;
      break;
    }
  }
  if (d == nullptr) {
    Log("usb: port %d: too many devices\n", port);
    return;
  }
  if (!g_xhci.ResetPort(port)) {
    Log("usb: port %d: device does not enable\n", port);
    return;
  }
  d->keyboard = false;
  if (!g_xhci.AddressDevice(port, &d->device, *g_arena)) {
    return;
  }
  d->present = true;
  g_keyboard_state[d - g_devices] = hid::BootKeyboard();
  SetUp(*d, *g_arena);
}

void Unplugged(UsbDevice& d, InputTracker& tracker) {
  Log("usb: port %d: %04x:%04x unplugged\n", d.device.port, d.info.vendor_id, d.info.product_id);
  if (d.keyboard) {
    const uint8_t none[8] = {};
    g_keyboard_state[&d - g_devices].Feed(none, tracker);  // release whatever it held
  }
  g_xhci.DisableSlot(d.device.slot);
  d.present = false;
  d.keyboard = false;
}

}  // namespace

void InitUsb(const BootInfo& boot, lib::Arena& arena) {
  pci::Device found[8];
  const int count = pci::FindAllByClass(0x0c, found, 8);
  for (int i = 0; i < count; ++i) {
    Log("usb: serial bus controller %04x:%04x class %06x at %02x:%02x.%x\n", found[i].vendor_id, found[i].device_id,
        found[i].class_code, found[i].bus, found[i].device, found[i].function);
  }
  for (int i = 0; i < count && !g_have_xhci; ++i) {
    if (found[i].class_code == kClassXhci) {
      g_have_xhci = g_xhci.Init(found[i], arena, boot.tsc_hz);
    }
  }
  if (!g_have_xhci) {
    Log("usb: no usable xHCI controller\n");
    return;
  }
  const int connected = g_xhci.WaitForPorts(1000);
  Log("usb: %d of %d ports connected; PORTSC:", connected, g_xhci.port_count());
  for (int port = 1; port <= g_xhci.port_count(); ++port) {
    Log("%s%d(%u)=%08x", port % 6 == 1 ? "\n  " : " ", port, g_xhci.Port(port).usb_major, g_xhci.RawPortStatus(port));
  }
  Log("\n");
  g_arena = &arena;
  g_tsc_hz = boot.tsc_hz;
  for (int port = 1; port <= g_xhci.port_count(); ++port) {
    if (g_xhci.Port(port).connected) {
      Enumerate(port);
    }
  }
  UpdateStatus();
}

void PollUsb(InputTracker& tracker) {
  if (!g_have_xhci || g_busy) {
    return;  // also when the crash screen polls after a fault inside this function
  }
  g_busy = true;
  xhci::TransferEvent e;
  while (g_xhci.PollTransfer(&e)) {
    for (int i = 0; i < kMaxDevices; ++i) {
      UsbDevice& d = g_devices[i];
      if (!d.present || !d.keyboard || d.device.slot != e.slot || d.keyboard_endpoint != e.endpoint) {
        continue;
      }
      if (e.code == xhci::Controller::kCompletionSuccess || e.code == xhci::Controller::kCompletionShortPacket) {
        uint8_t report[8];
        lib::Copy(report, d.report, 8);
        if (const uint8_t usage = g_keyboard_state[i].Feed(report, tracker)) {
          g_last_usage = usage;
        }
        g_xhci.QueueIn(d.device, d.keyboard_endpoint, d.keyboard_ring, d.report, 8);
      } else if (g_xhci.Port(d.device.port).connected) {
        Log("usb: keyboard on port %d: transfer error %d, stopped\n", d.device.port, e.code);
      }
    }
  }

  // Hot-plug: unplugged devices go at once, new ones are enumerated once they have settled.
  const uint32_t changes = g_xhci.TakePortChanges();
  const uint64_t now = NowMs();
  bool changed = false;
  for (int port = 1; port <= g_xhci.port_count(); ++port) {
    const uint32_t bit = 1u << (port - 1);
    if ((changes & bit) != 0) {
      const bool connected = g_xhci.Port(port).connected;
      UsbDevice* d = DeviceOnPort(port);
      if (d != nullptr && !connected) {
        Unplugged(*d, tracker);
        changed = true;
      }
      if (connected && d == nullptr) {
        g_waiting_ports |= bit;
        g_waiting_since_ms[port] = now;
      } else {
        g_waiting_ports &= ~bit;
      }
    }
    if ((g_waiting_ports & bit) != 0 && now - g_waiting_since_ms[port] >= kConnectSettleMs) {
      g_waiting_ports &= ~bit;
      if (g_xhci.Port(port).connected) {
        Log("usb: port %d: device plugged in\n", port);
        Enumerate(port);  // the reset's own change event is ignored later: the port then has a device
        changed = true;
      }
    }
  }
  if (changed) {
    UpdateStatus();
  }
  g_busy = false;
}

uint8_t UsbLastKey() { return g_last_usage; }

void StopUsb() {
  if (g_have_xhci) {
    g_xhci.Halt();
  }
}

const char* UsbStatus() { return g_status; }

}  // namespace platform
