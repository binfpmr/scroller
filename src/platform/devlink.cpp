#include "platform/devlink.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "lib/format.h"
#include "lib/mem.h"
#include "platform/gdb_stub.h"
#include "platform/reset.h"
#include "scroller_version.h"

extern "C" char __image_base[];  // efi.ld

namespace platform {
namespace {

constexpr size_t kHeader = 8;
constexpr size_t kLogChunk = 1024;
constexpr int kLogPacketsPerPoll = 4;

enum Command : uint8_t {
  kInfo = 0x01,
  kRead = 0x02,
  kWrite = 0x03,
  kKey = 0x04,
  kReboot = 0x05,
  kSubscribe = 0x06,
  kGdbPacket = 0x07,
  kGdbWait = 0x08,
  kGdbInterrupt = 0x09,
  kLog = 0x80,
};

struct State {
  net::Stack* stack = nullptr;
  const BootInfo* boot = nullptr;
  InputTracker* tracker = nullptr;
  const gfx::Surface* screen = nullptr;
  bool subscribed = false;
  net::Ip4 subscriber_ip;
  uint16_t subscriber_port = 0;
  uint64_t log_position = 0;
  bool reboot = false;
  uint64_t boot_id = 0;  // differs between boots, so the PC notices a reboot
  // Last GDB packet's sequence number and reply: a retransmitted packet gets the same reply instead of running
  // again (a repeated "s" would step twice).
  bool gdb_cached = false;
  uint32_t gdb_sequence = 0;
  size_t gdb_reply_length = 0;
  char gdb_reply[1024];
};

State g_state;

uint64_t Get64(const uint8_t* p) {
  uint64_t v = 0;
  lib::Copy(&v, p, 8);  // x86 is little endian like the protocol
  return v;
}
uint32_t Get32(const uint8_t* p) {
  uint32_t v = 0;
  lib::Copy(&v, p, 4);
  return v;
}

void Reply(const net::UdpDatagram& request, uint8_t status, const void* payload, size_t length) {
  uint8_t packet[kHeader + kMaxRead + 512];
  lib::Copy(packet, request.data, kHeader);
  packet[3] = status;
  lib::Copy(packet + kHeader, payload, length);
  g_state.stack->SendUdp(request.source_ip, request.source_port, kDevlinkPort, packet, kHeader + length);
}

size_t FormatInfo(char* out, size_t size) {
  const BootInfo& boot = *g_state.boot;
  const net::Config& c = g_state.stack->config();
  const net::Mac& m = g_state.stack->mac();
  return lib::Format(
      out, size,
      "version=%s\nboot_id=%llx\nimage_base=%p\nscreen=%p\nwidth=%d\nheight=%d\nstride=%d\ntsc_hz=%llu\n"
      "mac=%02x:%02x:%02x:%02x:%02x:%02x\nip=%u.%u.%u.%u\nlog_end=%llu\n",
      kScrollerVersion, static_cast<unsigned long long>(g_state.boot_id), static_cast<void*>(__image_base),
      static_cast<void*>(g_state.screen->pixels), g_state.screen->width, g_state.screen->height,
      g_state.screen->width * 4, static_cast<unsigned long long>(boot.tsc_hz), m.b[0], m.b[1], m.b[2], m.b[3], m.b[4],
      m.b[5], c.ip.b[0], c.ip.b[1], c.ip.b[2], c.ip.b[3], static_cast<unsigned long long>(debug_log::HistoryEnd()));
}

void OnDatagram(void*, const net::UdpDatagram& d) {
  if (d.length < kHeader || d.data[0] != 'D' || d.data[1] != 'L') {
    return;
  }
  const uint8_t* body = d.data + kHeader;
  const size_t body_length = d.length - kHeader;
  switch (d.data[2]) {
    case kInfo: {
      char text[512];
      const size_t n = FormatInfo(text, sizeof(text));
      Reply(d, 0, text, lib::Min(n, sizeof(text) - 1));
      break;
    }
    case kRead: {
      if (body_length < 12) {
        Reply(d, 2, nullptr, 0);
        break;
      }
      const uint64_t address = Get64(body);
      const uint32_t length = Get32(body + 8);
      if (length > kMaxRead || !x86::RangeMapped(address, length)) {
        Reply(d, 1, nullptr, 0);
        break;
      }
      Reply(d, 0, reinterpret_cast<const void*>(address), length);
      break;
    }
    case kWrite: {
      if (body_length < 8) {
        Reply(d, 2, nullptr, 0);
        break;
      }
      const uint64_t address = Get64(body);
      const size_t length = body_length - 8;
      if (!x86::RangeMapped(address, length)) {
        Reply(d, 1, nullptr, 0);
        break;
      }
      x86::CopyIgnoringWriteProtect(reinterpret_cast<void*>(address), body + 8, length);
      Reply(d, 0, nullptr, 0);
      break;
    }
    case kKey: {
      if (body_length < 2 || body[0] >= static_cast<uint8_t>(Key::kCount)) {
        Reply(d, 2, nullptr, 0);
        break;
      }
      g_state.tracker->OnKey(static_cast<Key>(body[0]), body[1] != 0);
      Reply(d, 0, nullptr, 0);
      break;
    }
    case kReboot:
      Log("devlink: reboot requested\n");
      Reply(d, 0, nullptr, 0);
      g_state.reboot = true;  // from PollDevlink, after the reply is out
      break;
    case kSubscribe:
      g_state.subscribed = true;
      g_state.subscriber_ip = d.source_ip;
      g_state.subscriber_port = d.source_port;
      g_state.log_position = body_length >= 1 && body[0] != 0 ? 0 : debug_log::HistoryEnd();
      Reply(d, 0, nullptr, 0);
      Log("devlink: log subscriber %u.%u.%u.%u:%u\n", d.source_ip.b[0], d.source_ip.b[1], d.source_ip.b[2],
          d.source_ip.b[3], d.source_port);
      break;
    case kGdbPacket: {
      const uint32_t sequence = Get32(d.data + 4);
      if (!g_state.gdb_cached || sequence != g_state.gdb_sequence) {
        const auto* packet = reinterpret_cast<const char*>(body);
        if (body_length == 1 && packet[0] == 'k') {
          g_state.reboot = true;  // kill: reboot (gdb expects no reply)
        }
        g_state.gdb_reply_length =
            GdbStubHandlePacket(packet, body_length, g_state.gdb_reply, sizeof(g_state.gdb_reply));
        g_state.gdb_sequence = sequence;
        g_state.gdb_cached = true;
      }
      Reply(d, 0, g_state.gdb_reply, g_state.gdb_reply_length);
      break;
    }
    case kGdbWait: {
      char stop[16];
      const size_t n = GdbStubWait(stop, sizeof(stop));
      Reply(d, n == 0 ? 4 : 0, stop, n);  // status 4: still running
      break;
    }
    case kGdbInterrupt:
      GdbStubRequestStop();
      Reply(d, 0, nullptr, 0);
      break;
    default:
      Reply(d, 3, nullptr, 0);
      break;
  }
}

}  // namespace

void InitDevlink(net::Stack* stack, const BootInfo& boot, InputTracker* tracker, const gfx::Surface* screen) {
  if (stack == nullptr) {
    return;
  }
  g_state.stack = stack;
  g_state.boot = &boot;
  g_state.tracker = tracker;
  g_state.screen = screen;
  g_state.boot_id = x86::Rdtsc();
  stack->Bind(kDevlinkPort, &OnDatagram, nullptr);
  Log("devlink: UDP port %u\n", kDevlinkPort);
}

void PollDevlink() {
  if (g_state.stack == nullptr) {
    return;
  }
  if (g_state.reboot) {
    Reboot(g_state.boot->runtime_services);
  }
  GdbStubPoll();
  if (!g_state.subscribed || !g_state.stack->configured()) {
    return;
  }
  for (int i = 0; i < kLogPacketsPerPoll; ++i) {
    uint8_t packet[kHeader + 8 + kLogChunk] = {'D', 'L', kLog, 0};
    uint64_t position = g_state.log_position;
    const size_t n = debug_log::ReadHistory(&position, reinterpret_cast<char*>(packet + kHeader + 8), kLogChunk);
    if (n == 0) {
      break;
    }
    lib::Copy(packet + kHeader, &position, 8);
    if (!g_state.stack->SendUdp(g_state.subscriber_ip, g_state.subscriber_port, kDevlinkPort, packet,
                                kHeader + 8 + n)) {
      break;  // transmit ring full: try again on the next poll
    }
    g_state.log_position = position + n;
  }
}

}  // namespace platform
