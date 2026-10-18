#include "platform/network.h"

#include "arch/x86_64/cpu.h"
#include "drivers/debug_log.h"
#include "drivers/e1000.h"
#include "drivers/rtl8168.h"
#include "lib/format.h"
#include "lib/mem.h"

namespace platform {
namespace {

constexpr int kFramesPerPoll = 16;

// constinit: the drivers have vtables, which must be set up statically (no global constructors run).
constinit e1000::Driver g_e1000;
constinit rtl8168::Driver g_rtl8168;
nic::Driver* g_nic = nullptr;
pci::Device g_device;
constinit net::Stack g_stack;
bool g_from_pxe = false;
uint64_t g_tsc_hz = 0;
bool g_was_configured = false;
char g_status[64] = "NONE";

uint64_t NowMs() { return x86::Rdtsc() / (g_tsc_hz / 1000); }

}  // namespace

void InitNetwork(const BootInfo& boot, lib::Arena& arena) {
  g_tsc_hz = boot.tsc_hz;
  pci::Device found[4];
  const int count = pci::FindAllByClass(0x02, found, 4);
  for (int i = 0; i < count && g_nic == nullptr; ++i) {
    const pci::Device& d = found[i];
    if (e1000::Supports(d) && g_e1000.Init(d, arena)) {
      g_nic = &g_e1000;
    } else if (rtl8168::Supports(d) && g_rtl8168.Init(d, arena)) {
      g_nic = &g_rtl8168;
    }
    if (g_nic != nullptr) {
      g_device = d;
    }
  }
  if (g_nic == nullptr) {
    Log("net: no supported Ethernet controller\n");
    return;
  }
  g_stack.Init(g_nic, g_nic->mac());
  if (boot.netboot.valid) {
    net::Config config;
    lib::Copy(config.ip.b, boot.netboot.ip, 4);
    lib::Copy(config.netmask.b, boot.netboot.netmask, 4);
    g_stack.Configure(config);
    g_from_pxe = true;
  } else {
    g_stack.StartDhcp(NowMs(), static_cast<uint32_t>(x86::Rdtsc()));
  }
  PollNetwork();
}

void PollNetwork() {
  if (g_nic == nullptr) {
    return;
  }
  const uint64_t now = NowMs();
  for (int i = 0; i < kFramesPerPoll; ++i) {
    const uint8_t* frame = nullptr;
    const size_t length = g_nic->Receive(&frame);
    if (length == 0) {
      break;
    }
    g_stack.HandleFrame(frame, length, now);
  }
  g_stack.Tick(now);
  if (g_stack.configured() && !g_was_configured) {
    const net::Config& c = g_stack.config();
    Log("net: %u.%u.%u.%u/%u.%u.%u.%u gateway %u.%u.%u.%u, %s\n", c.ip.b[0], c.ip.b[1], c.ip.b[2], c.ip.b[3],
        c.netmask.b[0], c.netmask.b[1], c.netmask.b[2], c.netmask.b[3], c.gateway.b[0], c.gateway.b[1], c.gateway.b[2],
        c.gateway.b[3], g_from_pxe ? "from PXE" : "by DHCP");
  }
  g_was_configured = g_stack.configured();
}

net::Stack* NetworkStack() { return g_nic != nullptr ? &g_stack : nullptr; }

const char* NetworkStatus() {
  if (g_nic == nullptr) {
    return "NONE";
  }
  const int speed = g_nic->LinkSpeed();
  const size_t used = lib::Format(g_status, sizeof(g_status), "ETH %04x:%04x ", g_device.vendor_id, g_device.device_id);
  if (speed == 0) {
    lib::Format(g_status + used, sizeof(g_status) - used, "NO LINK");
  } else if (!g_stack.configured()) {
    lib::Format(g_status + used, sizeof(g_status) - used, "%dM DHCP...", speed);
  } else {
    const net::Ip4& ip = g_stack.config().ip;
    lib::Format(g_status + used, sizeof(g_status) - used, "%dM %u.%u.%u.%u", speed, ip.b[0], ip.b[1], ip.b[2], ip.b[3]);
  }
  return g_status;
}

}  // namespace platform
