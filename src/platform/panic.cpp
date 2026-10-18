#include "platform/panic.h"

#include <stdarg.h>

#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/present.h"
#include "lib/format.h"
#include "platform/devlink.h"
#include "platform/gdb_stub.h"
#include "platform/network.h"
#include "platform/reset.h"
#include "platform/usb.h"
#include "scroller_version.h"

extern "C" char __image_base[];  // linker script: where the image was loaded
extern "C" char __image_end[];

namespace platform {
namespace {

gfx::Surface* g_screen = nullptr;
const gfx::Framebuffer* g_fb = nullptr;
const gfx::Viewport* g_viewport = nullptr;
KeyboardSource* g_keyboard = nullptr;
EFI_RUNTIME_SERVICES* g_runtime_services = nullptr;
volatile bool g_panicking = false;

constexpr int kMaxFrames = 10;

struct Text {
  char data[1024];
  size_t length = 0;
};

void Add(Text& t, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void Add(Text& t, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  t.length += lib::FormatV(t.data + t.length, t.length < sizeof(t.data) ? sizeof(t.data) - t.length : 0, fmt, args);
  va_end(args);
  if (t.length >= sizeof(t.data)) {
    t.length = sizeof(t.data) - 1;
  }
}

// Call sites from the frame-pointer chain (the build keeps frame pointers), as offsets into the image.
int Backtrace(uint64_t rbp, uint64_t* offsets) {
  const uint64_t base = reinterpret_cast<uint64_t>(__image_base);
  int n = 0;
  for (; n < kMaxFrames; ++n) {
    if (rbp == 0 || (rbp & 7) != 0 || !x86::IsMapped(rbp) || !x86::IsMapped(rbp + 15)) {
      break;
    }
    const uint64_t* frame = reinterpret_cast<const uint64_t*>(rbp);
    const uint64_t ret = frame[1];
    if (ret < base || ret >= reinterpret_cast<uint64_t>(__image_end)) {
      break;  // left our code (e.g. into the firmware that called efi_main)
    }
    offsets[n] = ret - 1 - base;  // inside the call instruction: a call to a noreturn function may be the last one
    if (frame[0] <= rbp) {
      ++n;
      break;  // the chain must go up the stack
    }
    rbp = frame[0];
  }
  return n;
}

}  // namespace

void SetPanicContext(gfx::Surface* screen, const gfx::Framebuffer* fb, const gfx::Viewport* viewport,
                     KeyboardSource* keyboard, EFI_RUNTIME_SERVICES* runtime_services) {
  g_screen = screen;
  g_fb = fb;
  g_viewport = viewport;
  g_keyboard = keyboard;
  g_runtime_services = runtime_services;
}

void OnException(x86::InterruptFrame& f) {
  if (GdbStubOnException(f)) {
    return;  // a breakpoint, single step, stop request, or a crash a debugger looked at: resume
  }
  if (g_panicking) {  // fault while reporting a fault: stop here
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  g_panicking = true;

  const uint64_t base = reinterpret_cast<uint64_t>(__image_base);
  uint64_t trace[kMaxFrames];
  const int frames = Backtrace(f.rbp, trace);
  using ull = unsigned long long;
  Text text;
  Add(text, "CPU EXCEPTION %llu: %s\n", static_cast<ull>(f.vector), x86::ExceptionName(f.vector));
  Add(text, "ERROR %llx  CR2 %016llx\n", static_cast<ull>(f.error_code), static_cast<ull>(x86::ReadCr2()));
  Add(text, "RIP %016llx  IMAGE+%llx\n", static_cast<ull>(f.rip), static_cast<ull>(f.rip - base));
  Add(text, "RSP %016llx  RFL %016llx\n", static_cast<ull>(f.rsp), static_cast<ull>(f.rflags));
  Add(text, "RAX %016llx  RBX %016llx\n", static_cast<ull>(f.rax), static_cast<ull>(f.rbx));
  Add(text, "RCX %016llx  RDX %016llx\n", static_cast<ull>(f.rcx), static_cast<ull>(f.rdx));
  Add(text, "RSI %016llx  RDI %016llx\n", static_cast<ull>(f.rsi), static_cast<ull>(f.rdi));
  Add(text, "RBP %016llx  R8  %016llx\n", static_cast<ull>(f.rbp), static_cast<ull>(f.r8));
  Add(text, "R9  %016llx  R10 %016llx\n", static_cast<ull>(f.r9), static_cast<ull>(f.r10));
  Add(text, "R11 %016llx  R12 %016llx\n", static_cast<ull>(f.r11), static_cast<ull>(f.r12));
  Add(text, "R13 %016llx  R14 %016llx\n", static_cast<ull>(f.r13), static_cast<ull>(f.r14));
  Add(text, "R15 %016llx  CS %llx SS %llx\n", static_cast<ull>(f.r15), static_cast<ull>(f.cs), static_cast<ull>(f.ss));
  Add(text, "CALLED FROM (IMAGE OFFSETS):\n");
  for (int i = 0; i < frames; ++i) {
    Add(text, " %llx", static_cast<ull>(trace[i]));
    if (i % 6 == 5) {
      Add(text, "\n");
    }
  }
  Add(text, "\nVERSION %s\nESC: REBOOT  CTRL+ALT+R: RELOAD\n", kScrollerVersion);
  debug_log::Write(text.data, text.length);  // Log() would truncate at 512 characters
  Log("decode: addr2line -f -C -e build/uefi/scroller.elf 0x%llx", static_cast<ull>(f.rip - base));
  for (int i = 0; i < frames; ++i) {
    Log(" 0x%llx", static_cast<ull>(trace[i]));
  }
  Log("\n");

  if (g_screen != nullptr) {
    gfx::FillRect(*g_screen, 0, 0, g_screen->width, g_screen->height, gfx::Rgb(0, 0, 120));
    gfx::DrawText(*g_screen, 4, 4, text.data, gfx::Rgb(255, 255, 255));
    gfx::Present(*g_screen, *g_fb, *g_viewport);
  }
  InputTracker tracker;
  for (;;) {
    // The debug link keeps working (log, screenshot, reboot) unless the fault was in the network code itself; a
    // second fault stops in the check above.
    PollNetwork();
    PollDevlink();
    PollUsb(tracker);
    if (g_keyboard != nullptr) {
      g_keyboard->Poll(tracker);
      const InputState input = tracker.NextTick();
      if (input.Pressed(Key::kEscape) || input.RebootRequested()) {  // nothing else to do here: Esc is enough
        Reboot(g_runtime_services);
      }
      if (input.ReloadRequested()) {
        RequestNetworkReload();  // carried out by PollDevlink above
      }
    }
    x86::Pause();
  }
}

}  // namespace platform
