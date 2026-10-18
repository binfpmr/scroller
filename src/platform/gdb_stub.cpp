#include "platform/gdb_stub.h"

#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "lib/format.h"
#include "lib/mem.h"
#include "platform/devlink.h"
#include "platform/network.h"
#include "platform/sound.h"

namespace platform {
namespace {

constexpr int kMaxBreakpoints = 32;
constexpr uint64_t kTrapFlag = 1u << 8;
constexpr uint64_t kInterruptFlag = 1u << 9;
constexpr uint8_t kInt3 = 0xcc;
constexpr int kSigInt = 2, kSigIll = 4, kSigTrap = 5, kSigFpe = 8, kSigSegv = 11;

struct Breakpoint {
  uint64_t address = 0;
  uint8_t saved = 0;
  bool used = false;
};

Breakpoint g_breakpoints[kMaxBreakpoints];
x86::InterruptFrame* g_frame = nullptr;  // set while stopped
volatile bool g_resume = false;
bool g_attached = false;
volatile bool g_stop_requested = false;
bool g_stopping_on_request = false;
bool g_stepping = false;
bool g_step_interrupts = false;  // IF before the step (the step runs with interrupts off)
int g_signal = 0;

int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

// Parses hex digits at *p up to a non-hex character; advances *p.
uint64_t ParseHex(const char** p, const char* end) {
  uint64_t v = 0;
  while (*p < end && HexDigit(**p) >= 0) {
    v = v << 4 | static_cast<uint64_t>(HexDigit(**p));
    ++*p;
  }
  return v;
}

// Appends `length` bytes as hex; returns the new position.
char* PutHex(char* out, const char* end, const void* data, size_t length) {
  static constexpr char kDigits[] = "0123456789abcdef";
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < length && out + 2 <= end; ++i) {
    *out++ = kDigits[bytes[i] >> 4];
    *out++ = kDigits[bytes[i] & 0xf];
  }
  return out;
}

size_t Text(char* reply, size_t size, const char* text) {
  return lib::Min(lib::Format(reply, size, "%s", text), size - 1);
}

Breakpoint* FindBreakpoint(uint64_t address) {
  for (Breakpoint& b : g_breakpoints) {
    if (b.used && b.address == address) {
      return &b;
    }
  }
  return nullptr;
}

bool InsertBreakpoint(uint64_t address) {
  if (FindBreakpoint(address) != nullptr) {
    return true;  // gdb may insert twice (a retried packet)
  }
  if (!x86::RangeMapped(address, 1)) {
    return false;
  }
  for (Breakpoint& b : g_breakpoints) {
    if (!b.used) {
      b = {.address = address, .saved = *reinterpret_cast<const uint8_t*>(address), .used = true};
      x86::CopyIgnoringWriteProtect(reinterpret_cast<void*>(address), &kInt3, 1);
      return true;
    }
  }
  return false;
}

bool RemoveBreakpoint(uint64_t address) {
  Breakpoint* b = FindBreakpoint(address);
  if (b == nullptr) {
    return true;
  }
  x86::CopyIgnoringWriteProtect(reinterpret_cast<void*>(address), &b->saved, 1);
  b->used = false;
  return true;
}

uint32_t SegmentRegister(int index) {
  uint16_t v = 0;
  switch (index) {
    case 0:
      asm volatile("mov %%ds, %0" : "=r"(v));
      break;
    case 1:
      asm volatile("mov %%es, %0" : "=r"(v));
      break;
    case 2:
      asm volatile("mov %%fs, %0" : "=r"(v));
      break;
    default:
      asm volatile("mov %%gs, %0" : "=r"(v));
      break;
  }
  return v;
}

// gdb's amd64 register numbers 0..15 in frame order.
uint64_t* GeneralRegister(x86::InterruptFrame& f, int n) {
  uint64_t* const regs[16] = {&f.rax, &f.rbx, &f.rcx, &f.rdx, &f.rsi, &f.rdi, &f.rbp, &f.rsp,
                              &f.r8,  &f.r9,  &f.r10, &f.r11, &f.r12, &f.r13, &f.r14, &f.r15};
  return regs[n];
}

// 'g': rax rbx rcx rdx rsi rdi rbp rsp r8..r15 rip (8 bytes each), eflags cs ss ds es fs gs (4 bytes each). gdb
// treats the registers it expects after these (x87, SSE) as unavailable.
size_t ReadRegisters(char* reply, size_t size) {
  x86::InterruptFrame& f = *g_frame;
  char* out = reply;
  char* const end = reply + size;
  for (int n = 0; n < 16; ++n) {
    out = PutHex(out, end, GeneralRegister(f, n), 8);
  }
  out = PutHex(out, end, &f.rip, 8);
  const uint32_t small[7] = {static_cast<uint32_t>(f.rflags),
                             static_cast<uint32_t>(f.cs),
                             static_cast<uint32_t>(f.ss),
                             SegmentRegister(0),
                             SegmentRegister(1),
                             SegmentRegister(2),
                             SegmentRegister(3)};
  for (uint32_t v : small) {
    out = PutHex(out, end, &v, 4);
  }
  return static_cast<size_t>(out - reply);
}

// Little-endian hex of `bytes` bytes.
uint64_t ParseLittleEndian(const char* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) {
    const int hi = HexDigit(p[2 * i]);
    const int lo = HexDigit(p[2 * i + 1]);
    v |= static_cast<uint64_t>((hi << 4) | lo) << (8 * i);
  }
  return v;
}

bool WriteRegisters(const char* p, size_t length) {
  if (length < 2 * (17 * 8 + 4)) {
    return false;
  }
  x86::InterruptFrame& f = *g_frame;
  for (int n = 0; n < 16; ++n) {
    *GeneralRegister(f, n) = ParseLittleEndian(p + 16 * n, 8);
  }
  f.rip = ParseLittleEndian(p + 16 * 16, 8);
  f.rflags = ParseLittleEndian(p + 16 * 17, 4);  // segment selectors stay as they are
  return true;
}

void Resume(bool step, const char* p, const char* end) {
  if (p < end) {
    g_frame->rip = ParseHex(&p, end);  // "c addr" / "s addr"
  }
  g_stepping = step;
  if (step) {
    g_step_interrupts = (g_frame->rflags & kInterruptFlag) != 0;
    g_frame->rflags = (g_frame->rflags | kTrapFlag) & ~kInterruptFlag;  // no detour into an interrupt handler
  }
  g_resume = true;
}

int SignalFor(uint64_t vector) {
  switch (vector) {
    case 0:
      return kSigFpe;
    case 1:
    case 3:
      return kSigTrap;
    case 6:
      return kSigIll;
    default:
      return kSigSegv;
  }
}

}  // namespace

bool GdbStubOnException(x86::InterruptFrame& frame) {
  if (!g_attached && frame.vector == 1 && g_frame == nullptr) {
    frame.rflags &= ~kTrapFlag;  // a single step still armed when the debugger left: just carry on
    return true;
  }
  if (!g_attached || g_frame != nullptr || NetworkBusy()) {
    return false;  // no debugger, a fault inside the stub, or the network is in use: a normal crash report
  }
  g_signal = SignalFor(frame.vector);
  if (frame.vector == 3) {
    if (FindBreakpoint(frame.rip - 1) != nullptr) {
      frame.rip -= 1;  // report (and later resume) at the breakpoint itself
    } else if (g_stopping_on_request) {
      g_signal = kSigInt;
    }
  } else if (frame.vector == 1) {
    frame.rflags &= ~kTrapFlag;
    if (g_stepping && g_step_interrupts) {
      frame.rflags |= kInterruptFlag;
    }
  }
  g_stopping_on_request = false;
  g_stepping = false;
  if (frame.vector != 1) {  // single steps would flood the log
    Log("gdb: stopped (signal %d) at %p\n", g_signal, reinterpret_cast<void*>(frame.rip));
  }
  PauseSound();
  g_frame = &frame;
  g_resume = false;
  while (!g_resume) {
    PollNetwork();
    PollDevlink();
    asm volatile("pause");
  }
  g_frame = nullptr;
  ResumeSound();
  return true;
}

size_t GdbStubWait(char* reply, size_t reply_size) {
  if (g_frame == nullptr) {
    return 0;
  }
  return lib::Min(lib::Format(reply, reply_size, "S%02x", g_signal), reply_size - 1);
}

void GdbStubRequestStop() {
  g_attached = true;
  if (g_frame == nullptr) {
    g_stop_requested = true;
  }
}

void GdbStubPoll() {
  if (g_stop_requested && g_frame == nullptr) {
    g_stop_requested = false;
    g_stopping_on_request = true;
    asm volatile("int3");
  }
}

size_t GdbStubHandlePacket(const char* packet, size_t length, char* reply, size_t reply_size) {
  const char* p = packet;
  const char* const end = packet + length;
  auto starts = [&](const char* prefix) {
    size_t n = 0;
    while (prefix[n] != '\0') {
      if (n >= length || packet[n] != prefix[n]) {
        return false;
      }
      ++n;
    }
    return true;
  };
  if (starts("qSupported")) {
    return Text(reply, reply_size, "PacketSize=3e8;qXfer:features:read+");
  }
  if (starts("qXfer:features:read:target.xml:")) {
    // Only the architecture: gdb then uses its standard amd64 register layout, even without an executable loaded.
    static constexpr char kTargetXml[] =
        "<?xml version=\"1.0\"?><!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
        "<target><architecture>i386:x86-64</architecture></target>";
    p += 31;
    const uint64_t offset = ParseHex(&p, end);
    if (offset >= sizeof(kTargetXml) - 1) {
      return Text(reply, reply_size, "l");
    }
    return lib::Min(lib::Format(reply, reply_size, "l%s", kTargetXml + offset), reply_size - 1);
  }
  if (starts("qAttached")) {
    return Text(reply, reply_size, "1");
  }
  if (starts("qfThreadInfo")) {
    return Text(reply, reply_size, "m1");
  }
  if (starts("qsThreadInfo")) {
    return Text(reply, reply_size, "l");
  }
  if (starts("qC")) {
    return Text(reply, reply_size, "QC1");
  }
  if (length == 0 || packet[0] == 'v' || packet[0] == 'q' || packet[0] == 'Q') {
    return 0;  // unsupported: empty reply
  }
  if (packet[0] == 'H' || packet[0] == 'T') {
    return Text(reply, reply_size, "OK");
  }
  if (g_frame == nullptr) {
    return Text(reply, reply_size, "E01");  // the rest needs a stopped machine
  }
  switch (packet[0]) {
    case '?':
      return GdbStubWait(reply, reply_size);
    case 'g':
      return ReadRegisters(reply, reply_size);
    case 'G':
      return Text(reply, reply_size, WriteRegisters(p + 1, length - 1) ? "OK" : "E01");
    case 'm': {
      ++p;
      const uint64_t address = ParseHex(&p, end);
      ++p;  // ','
      const uint64_t count = lib::Min<uint64_t>(ParseHex(&p, end), (reply_size - 1) / 2);
      if (!x86::RangeMapped(address, count)) {
        return Text(reply, reply_size, "E14");
      }
      return static_cast<size_t>(PutHex(reply, reply + reply_size, reinterpret_cast<const void*>(address), count) -
                                 reply);
    }
    case 'M': {
      ++p;
      const uint64_t address = ParseHex(&p, end);
      ++p;  // ','
      const uint64_t count = ParseHex(&p, end);
      ++p;  // ':'
      if (static_cast<uint64_t>(end - p) < 2 * count || !x86::RangeMapped(address, count)) {
        return Text(reply, reply_size, "E14");
      }
      for (uint64_t i = 0; i < count; ++i) {
        const auto byte = static_cast<uint8_t>(ParseLittleEndian(p + 2 * i, 1));
        x86::CopyIgnoringWriteProtect(reinterpret_cast<void*>(address + i), &byte, 1);
      }
      return Text(reply, reply_size, "OK");
    }
    case 'Z':
    case 'z': {
      if (length < 2 || packet[1] != '0') {
        return 0;  // only software breakpoints
      }
      p += 3;  // "Z0,"
      const uint64_t address = ParseHex(&p, end);
      const bool ok = packet[0] == 'Z' ? InsertBreakpoint(address) : RemoveBreakpoint(address);
      return Text(reply, reply_size, ok ? "OK" : "E01");
    }
    case 'c':
      Resume(false, p + 1, end);
      return 0;
    case 's':
      Resume(true, p + 1, end);
      return 0;
    case 'D':
      for (Breakpoint& b : g_breakpoints) {
        if (b.used) {
          RemoveBreakpoint(b.address);
        }
      }
      g_attached = false;
      Resume(false, end, end);
      return Text(reply, reply_size, "OK");
    default:
      return 0;
  }
}

}  // namespace platform
