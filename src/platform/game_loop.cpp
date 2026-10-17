#include "platform/game_loop.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"
#include "game/scroll_demo.h"
#include "gfx/present.h"
#include "lib/arena.h"
#include "platform/reset.h"

namespace platform {
namespace {

constexpr int kTicksPerSecond = 60;
constexpr int kMaxCatchUpTicks = 5;  // after a stall, skip time rather than run many updates at once
constexpr int kStatsIntervalTicks = 5 * kTicksPerSecond;

[[noreturn]] void Fatal(const char* message) {
  Log("fatal: %s\n", message);
  for (;;) {
    x86::Halt();
  }
}

}  // namespace

void RunGame(const BootInfo& boot, KeyboardSource& keyboard) {
  lib::Arena arena(boot.memory, boot.memory_size);
  gfx::Surface screen{.pixels = arena.AllocateArray<gfx::Color>(gfx::kScreenWidth * gfx::kScreenHeight),
                      .width = gfx::kScreenWidth,
                      .height = gfx::kScreenHeight};
  if (screen.pixels == nullptr) {
    Fatal("out of memory for the screen surface");
  }

  const gfx::Framebuffer& fb = boot.framebuffer;
  const gfx::Viewport viewport = gfx::ComputeViewport(fb.width, fb.height);
  gfx::ClearFramebuffer(fb, gfx::Rgb(0, 0, 0));
  Log("viewport: scale %d at %d,%d\n", viewport.scale, viewport.x, viewport.y);

  game::ScrollDemo demo;
  InputTracker tracker;
  const uint64_t tick_cycles = boot.tsc_hz / kTicksPerSecond;
  uint64_t next_tick = x86::Rdtsc();
  uint64_t ticks = 0;
  uint64_t frames = 0;
  uint64_t present_cycles = 0;
  Log("boot ok: game running\n");

  for (;;) {
    keyboard.Poll(tracker);
    const uint64_t now = x86::Rdtsc();
    if (now < next_tick) {
      x86::Pause();
      continue;
    }

    for (int step = 0; step < kMaxCatchUpTicks && now >= next_tick; ++step) {
      const InputState input = tracker.NextTick();
      if (input.Pressed(Key::kEscape)) {
        Reboot(boot.runtime_services);
      }
      demo.Update(input);
      next_tick += tick_cycles;
      ++ticks;
      if (ticks % kStatsIntervalTicks == 0) {
        Log("tick %llu: %llu frames, present %llu us avg, camera %d,%d\n", static_cast<unsigned long long>(ticks),
            static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(present_cycles * 1000000 / boot.tsc_hz / (frames ? frames : 1)),
            demo.camera_x(), demo.camera_y());
        frames = 0;
        present_cycles = 0;
      }
    }
    if (now >= next_tick) {
      next_tick = now + tick_cycles;  // fell too far behind
    }

    demo.Render(screen);
    const uint64_t present_start = x86::Rdtsc();
    gfx::Present(screen, fb, viewport);
    present_cycles += x86::Rdtsc() - present_start;
    ++frames;
  }
}

}  // namespace platform
