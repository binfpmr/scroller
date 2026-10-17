#include "platform/game_loop.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/write_combining.h"
#include "drivers/debug_log.h"
#include "drivers/pci.h"
#include "game/game.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/present.h"
#include "gfx/sprite.h"
#include "lib/arena.h"
#include "lib/format.h"
#include "lib/pack.h"
#include "platform/reset.h"
#include "platform/stats_overlay.h"
#include "platform/vsync.h"

namespace platform {
namespace {

constexpr int kTicksPerSecond = 60;
constexpr int kMaxCatchUpTicks = 5;  // after a stall, skip time rather than run many updates at once
constexpr int kStatsIntervalTicks = 5 * kTicksPerSecond;

constexpr const char* kStartLevel = "levels/level1.txt";

// Parses one sprite sheet from the pack; on failure formats "name line N: problem" into `message`.
bool LoadSheet(const lib::Pack& pack, const char* name, lib::Arena& arena, gfx::SpriteSheet* sheet, char* message,
               size_t message_size) {
  lib::Blob blob;
  if (!pack.Find(name, &blob)) {
    lib::Format(message, message_size, "%s not in the asset pack", name);
    return false;
  }
  const char* error = nullptr;
  int line = 0;
  if (!sheet->Parse(blob.data, blob.size, arena, &error, &line)) {
    lib::Format(message, message_size, "%s line %d:\n%s", name, line, error);
    return false;
  }
  return true;
}

[[noreturn]] void Halt(const char* message) {
  Log("fatal: %s\n", message);
  for (;;) {
    x86::Halt();
  }
}

// Shows an error on screen (there is no other output on real hardware) and waits for Esc to reboot.
[[noreturn]] void Fatal(const char* message, gfx::Surface& screen, const gfx::Framebuffer& fb,
                        const gfx::Viewport& viewport, KeyboardSource& keyboard, const BootInfo& boot) {
  Log("fatal: %s\n", message);
  gfx::FillRect(screen, 0, 0, screen.width, screen.height, gfx::Rgb(90, 0, 0));
  gfx::DrawText(screen, 8, 8, "SCROLLER CANNOT START:", gfx::Rgb(255, 255, 255));
  gfx::DrawText(screen, 8, 8 + 2 * gfx::kLineHeight, message, gfx::Rgb(255, 220, 120));
  gfx::DrawText(screen, 8, 8 + 4 * gfx::kLineHeight, "ESC: REBOOT", gfx::Rgb(255, 255, 255));
  gfx::Present(screen, fb, viewport);
  InputTracker tracker;
  for (;;) {
    keyboard.Poll(tracker);
    if (tracker.NextTick().Pressed(Key::kEscape)) {
      Reboot(boot.runtime_services);
    }
    x86::Pause();
  }
}

}  // namespace

void RunGame(const BootInfo& boot, KeyboardSource& keyboard) {
  lib::Arena arena(boot.memory, boot.memory_size);
  gfx::Surface screen{.pixels = arena.AllocateArray<gfx::Color>(gfx::kScreenWidth * gfx::kScreenHeight),
                      .width = gfx::kScreenWidth,
                      .height = gfx::kScreenHeight};
  if (screen.pixels == nullptr) {
    Halt("out of memory for the screen surface");
  }

  const gfx::Framebuffer& fb = boot.framebuffer;
  // Write-combining makes presenting ~400x faster on real hardware (uncached framebuffer: 167 ms per frame on an HP
  // Pavilion). Our page table edit is only safe once the firmware is gone.
  bool write_combining = false;
  if (boot.owns_machine) {
    const uint64_t fb_size = fb.size != 0 ? fb.size : static_cast<uint64_t>(fb.stride) * fb.height * 4;
    write_combining = x86::EnableWriteCombining(arena, reinterpret_cast<uint64_t>(fb.base), fb_size);
  }
  const gfx::Viewport viewport = gfx::ComputeViewport(fb.width, fb.height);
  gfx::ClearFramebuffer(fb, gfx::Rgb(0, 0, 0));
  Log("viewport: scale %d at %d,%d\n", viewport.scale, viewport.x, viewport.y);

  const char* error = nullptr;
  if (boot.assets == nullptr) {
    Fatal("/SCROLLER/ASSETS.PAK NOT FOUND ON THE BOOT VOLUME", screen, fb, viewport, keyboard, boot);
  }
  lib::Pack pack;
  if (!pack.Open(boot.assets, boot.assets_size, &error)) {
    Fatal(error, screen, fb, viewport, keyboard, boot);
  }
  Log("assets: %d files, %llu bytes\n", pack.count(), static_cast<unsigned long long>(boot.assets_size));
  lib::Blob level_text;
  if (!pack.Find(kStartLevel, &level_text)) {
    Fatal("level not found in the asset pack", screen, fb, viewport, keyboard, boot);
  }
  static gfx::SpriteSheet player_sheet;
  static gfx::SpriteSheet tile_sheet;
  char message[160];
  if (!LoadSheet(pack, "sprites/player.txt", arena, &player_sheet, message, sizeof(message)) ||
      !LoadSheet(pack, "sprites/tiles.txt", arena, &tile_sheet, message, sizeof(message))) {
    Fatal(message, screen, fb, viewport, keyboard, boot);
  }
  game::Game game;
  if (!game.Init(arena, level_text.data, level_text.size, {.player = &player_sheet, .tiles = &tile_sheet}, &error)) {
    Fatal(error, screen, fb, viewport, keyboard, boot);
  }
  Log("level: %dx%d tiles, start %d,%d\n", game.level().width, game.level().height, game.level().start_x,
      game.level().start_y);

  pci::Device gpu;
  const bool has_gpu = pci::FindByClass(0x03, &gpu);
  Vsync vsync;
  vsync.Init(has_gpu ? &gpu : nullptr, boot.tsc_hz, boot.owns_machine ? &arena : nullptr);

  InputTracker tracker;
  StatsOverlay stats(boot.tsc_hz);
  {
    char cpu[49];
    x86::CpuBrand(cpu);
    char info[240];
    lib::Format(info, sizeof(info), "%s\nGPU %04x:%04x AT %02x:%02x.%x  VSYNC %s%s%s", cpu, has_gpu ? gpu.vendor_id : 0,
                has_gpu ? gpu.device_id : 0, gpu.bus, gpu.device, gpu.function, vsync.description(),
                !vsync.active() && vsync.intel_status()[0] != '\0' ? "\nINTEL " : "",
                vsync.active() ? "" : vsync.intel_status());
    Log("hw: %s\n", info);
    stats.SetInfo(info);
  }
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
      if (input.Pressed(Key::kToggleStats)) {
        stats.visible = !stats.visible;
      }
      game.Update(input);
      next_tick += tick_cycles;
      ++ticks;
      if (ticks % kStatsIntervalTicks == 0) {
        Log("tick %llu: %llu frames, present %llu us avg, player %d,%d, camera %d,%d\n",
            static_cast<unsigned long long>(ticks), static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(present_cycles * 1000000 / boot.tsc_hz / (frames ? frames : 1)),
            game.player().pixel_x(), game.player().pixel_y(), game.camera().x, game.camera().y);
        frames = 0;
        present_cycles = 0;
      }
    }
    if (now >= next_tick) {
      next_tick = now + tick_cycles;  // fell too far behind
    }

    const uint64_t render_start = x86::Rdtsc();
    game.Render(screen);
    stats.Draw(screen);
    const uint64_t render_end = x86::Rdtsc();
    vsync.WaitForFrameStart();
    const uint64_t present_start = x86::Rdtsc();
    gfx::Present(screen, fb, viewport);
    const uint64_t present_end = x86::Rdtsc();
    present_cycles += present_end - present_start;
    ++frames;
    stats.AddFrame(render_end - render_start, present_start - render_end, present_end - present_start);
    stats.Update(present_end, write_combining ? "ON" : "OFF");
  }
}

}  // namespace platform
