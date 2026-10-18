#include "platform/usb.h"

#include "drivers/debug_log.h"
#include "drivers/xhci.h"

namespace platform {
namespace {

constexpr uint32_t kClassXhci = 0x0c0330;  // serial bus, USB, xHCI

constinit xhci::Controller g_xhci;
bool g_have_xhci = false;

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
  for (int port = 1; port <= g_xhci.port_count(); ++port) {
    const xhci::PortStatus s = g_xhci.Port(port);
    if (s.connected) {
      Log("usb: port %d (USB %u): device, %s speed%s\n", port, s.usb_major, xhci::SpeedName(s.speed),
          s.enabled ? ", enabled" : "");
    }
  }
}

const char* UsbStatus() { return g_have_xhci ? g_xhci.description() : "NONE"; }

}  // namespace platform
