#include "platform/usb.h"

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

// USB requests.
constexpr uint8_t kGetDescriptor = 6, kSetConfiguration = 9, kHidSetIdle = 0x0a, kHidSetProtocol = 0x0b;

struct UsbDevice {
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
int g_device_count = 0;
int g_keyboards = 0;
hid::BootKeyboard g_keyboard_state[kMaxDevices];
uint8_t g_last_usage = 0;
char g_status[96] = "NONE";

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
  d.report = static_cast<uint8_t*>(arena.Allocate(64, 64));
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
  ++g_keyboards;
  Log("usb: port %d: boot keyboard, endpoint %02x, %u bytes every %u\n", dev.port, ep->address, ep->max_packet,
      ep->interval);
}

}  // namespace

void InitUsb(const BootInfo& boot, lib::Arena& arena) {
  pci::Device found[8];
  const int count = pci::FindAllByClass(0x0c, found, 8);
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
  Log("usb: %d of %d ports connected\n", connected, g_xhci.port_count());
  for (int port = 1; port <= g_xhci.port_count() && g_device_count < kMaxDevices; ++port) {
    if (!g_xhci.Port(port).connected) {
      continue;
    }
    if (!g_xhci.ResetPort(port)) {
      Log("usb: port %d: device does not enable\n", port);
      continue;
    }
    UsbDevice& d = g_devices[g_device_count];
    if (!g_xhci.AddressDevice(port, &d.device, arena)) {
      continue;
    }
    ++g_device_count;
    SetUp(d, arena);
  }
  lib::Format(g_status, sizeof(g_status), "%s  %d DEV  %d KBD", g_xhci.description(), g_device_count, g_keyboards);
}

void PollUsb(InputTracker& tracker) {
  if (g_keyboards == 0) {
    return;
  }
  xhci::TransferEvent e;
  while (g_xhci.PollTransfer(&e)) {
    for (int i = 0; i < g_device_count; ++i) {
      UsbDevice& d = g_devices[i];
      if (!d.keyboard || d.device.slot != e.slot || d.keyboard_endpoint != e.endpoint) {
        continue;
      }
      if (e.code == xhci::Controller::kCompletionSuccess || e.code == xhci::Controller::kCompletionShortPacket) {
        uint8_t report[8];
        lib::Copy(report, d.report, 8);
        if (const uint8_t usage = g_keyboard_state[i].Feed(report, tracker)) {
          g_last_usage = usage;
        }
        g_xhci.QueueIn(d.device, d.keyboard_endpoint, d.keyboard_ring, d.report, 8);
      } else {
        Log("usb: keyboard on port %d: transfer error %d, stopped\n", d.device.port, e.code);
      }
    }
  }
}

uint8_t UsbLastKey() { return g_last_usage; }

const char* UsbStatus() { return g_status; }

}  // namespace platform
