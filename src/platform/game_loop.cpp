#include "platform/game_loop.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/interrupts.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/write_combining.h"
#include "drivers/acpi.h"
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
#include "platform/interrupt_controllers.h"
#include "platform/network.h"
#include "platform/panic.h"
#include "platform/reset.h"
#include "platform/sound.h"
#include "platform/stats_overlay.h"
#include "platform/vsync.h"

namespace platform {
namespace {

constexpr int kTicksPerSecond = 60;
constexpr int kMaxCatchUpTicks = 5;  // after a stall, skip time rather than run many updates at once
constexpr int kStatsIntervalTicks = 5 * kTicksPerSecond;

constexpr const char* kStartLevel = "levels/level1.txt";

// Keyboard polling for the main loop and the vsync wait. On real hardware every call polls: the i8042 holds one byte
// at a time and the keyboard sends the next only after it was read, so reading late delays input (it felt sluggish
// with 1 kHz polling plus a vsync wait of most of the frame). In a VM each port access is a VM exit competing with
// QEMU's display thread for its global lock (frames dropped to ~35 FPS on a scaled screen), so there it polls at most
// once per millisecond.
class InputPoller {
 public:
  InputPoller(KeyboardSource& keyboard, InputTracker& tracker, uint64_t tsc_hz)
      : keyboard_(keyboard), tracker_(tracker), interval_(x86::UnderHypervisor() ? tsc_hz / 1000 : 0) {}

  void Poll() {
    const uint64_t now = x86::Rdtsc();
    if (now >= next_) {
      keyboard_.Poll(tracker_);
      next_ = now + interval_;
    }
  }
  static void Callback(void* poller) { static_cast<InputPoller*>(poller)->Poll(); }

 private:
  KeyboardSource& keyboard_;
  InputTracker& tracker_;
  uint64_t interval_;
  uint64_t next_ = 0;
};

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
  SetPanicContext(&screen, &fb, &viewport, &keyboard, boot.runtime_services);
  // Our IDT must be loaded before any interrupt controller is enabled.
  if (boot.owns_machine && !x86::InitInterrupts(arena, &OnException)) {
    Halt("cannot set up interrupt tables");
  }

  acpi::Madt madt;
  const uint8_t* madt_table = acpi::FindTable(boot.acpi_rsdp, "APIC");
  const bool have_madt = madt_table != nullptr && acpi::ParseMadt(madt_table, &madt);
  if (have_madt) {
    Log("acpi: MADT: %d CPUs, local APIC %llx, %d IOAPIC(s), %d ISA overrides, 8259 %s\n", madt.cpu_count,
        static_cast<unsigned long long>(madt.local_apic_address), madt.io_apic_count, madt.override_count,
        madt.has_8259 ? "present" : "absent");
    for (int i = 0; i < madt.io_apic_count; ++i) {
      Log("acpi:   IOAPIC id %u at %llx, GSI base %u\n", madt.io_apics[i].id,
          static_cast<unsigned long long>(madt.io_apics[i].address), madt.io_apics[i].gsi_base);
    }
    for (int i = 0; i < madt.override_count; ++i) {
      Log("acpi:   ISA IRQ %u -> GSI %u flags %x\n", madt.overrides[i].irq, madt.overrides[i].gsi,
          madt.overrides[i].flags);
    }
  } else {
    Log("acpi: no usable MADT\n");
  }
  const bool interrupts =
      boot.owns_machine && SetUpInterruptControllers(have_madt ? &madt : nullptr, arena, boot.tsc_hz);
  Log("interrupts: %s\n", interrupts ? "on" : "off");
  if (interrupts) {
    keyboard.StartInterrupts(kKeyboardVector, boot.tsc_hz);
  }
  Log("keyboard: %s\n", keyboard.mode());
  Log("viewport: scale %d at %d,%d\n", viewport.scale, viewport.x, viewport.y);

  const char* error = nullptr;
  if (boot.assets == nullptr) {
    Fatal("NO ASSET PACK IN THE IMAGE", screen, fb, viewport, keyboard, boot);
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
  if (interrupts) {
    InitSound(boot, pack, arena);
  }
  Log("sound: %s\n", SoundStatus());
  if (boot.owns_machine) {
    InitNetwork(boot, arena);  // in the uefi-input build the firmware's network driver still owns the controller
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
    char netboot[48] = "LOCAL BOOT";
    if (boot.netboot.valid) {
      const auto& n = boot.netboot;
      lib::Format(netboot, sizeof(netboot), "NETBOOT %u.%u.%u.%u FROM %u.%u.%u.%u", n.ip[0], n.ip[1], n.ip[2], n.ip[3],
                  n.server[0], n.server[1], n.server[2], n.server[3]);
    }
    // Every network controller, so the wired one shows up next to Wi-Fi (subclass 0x00 Ethernet, 0x80 other).
    char nics[128] = "";
    size_t used = 0;
    pci::Device found[4];
    const int nic_count = pci::FindAllByClass(0x02, found, 4);
    for (int i = 0; i < nic_count && used < sizeof(nics); ++i) {
      const uint8_t subclass = (found[i].class_code >> 8) & 0xff;
      used += lib::Format(nics + used, sizeof(nics) - used, "\nNIC %04x:%04x %s AT %02x:%02x.%x", found[i].vendor_id,
                          found[i].device_id,
                          subclass == 0x00   ? "ETH"
                          : subclass == 0x80 ? "WIFI"
                                             : "NET",
                          found[i].bus, found[i].device, found[i].function);
    }
    char info[320];
    lib::Format(info, sizeof(info), "%s\n%s%s\nGPU %04x:%04x AT %02x:%02x.%x  VSYNC %s%s%s", cpu, netboot,
                nic_count == 0 ? "\nNIC NONE" : nics, has_gpu ? gpu.vendor_id : 0, has_gpu ? gpu.device_id : 0, gpu.bus,
                gpu.device, gpu.function, vsync.description(),
                !vsync.active() && vsync.intel_status()[0] != '\0' ? "\nINTEL " : "",
                vsync.active() ? "" : vsync.intel_status());
    Log("hw: %s\n", info);
    stats.SetInfo(info);
  }
  const uint64_t tick_cycles = boot.tsc_hz / kTicksPerSecond;
  InputPoller input(keyboard, tracker, boot.tsc_hz);
  uint64_t next_tick = x86::Rdtsc();
  uint64_t ticks = 0;
  uint64_t volume_shown_until = 0;
  uint64_t frames = 0;
  uint64_t present_cycles = 0;
  Log("boot ok: game running\n");

  for (;;) {
    input.Poll();
    PollNetwork();
    const uint64_t now = x86::Rdtsc();
    if (now < next_tick) {
      if (TimerHz() != 0) {
        x86::Halt();  // sleep until the next interrupt (the 1 kHz timer at the latest)
      } else {
        x86::Pause();
      }
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
      if (input.Pressed(Key::kVolumeUp) || input.Pressed(Key::kVolumeDown) || input.Pressed(Key::kVolumeMute)) {
        if (input.Pressed(Key::kVolumeMute)) {
          SetMuted(!Muted());
        } else {
          SetMuted(false);
          SetVolume(Volume() + (input.Pressed(Key::kVolumeUp) ? 10 : -10));
        }
        volume_shown_until = ticks + 90;  // 1.5 s
      }
      game.Update(input);
      PlaySoundEvents(game.TakeEvents());
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
    if (ticks < volume_shown_until) {
      char text[24];
      lib::Format(text, sizeof(text), Muted() ? "VOLUME: MUTED" : "VOLUME %d%%", Volume());
      const int w = gfx::TextWidth(text) + 8;
      gfx::FillRect(screen, (screen.width - w) / 2, 8, w, gfx::kLineHeight + 4, gfx::Rgb(20, 20, 40));
      gfx::DrawText(screen, (screen.width - w) / 2 + 4, 10, text, gfx::Rgb(255, 255, 255));
    }
    const uint64_t render_end = x86::Rdtsc();
    vsync.WaitForFrameStart(&InputPoller::Callback, &input);
    const uint64_t present_start = x86::Rdtsc();
    gfx::Present(screen, fb, viewport);
    const uint64_t present_end = x86::Rdtsc();
    present_cycles += present_end - present_start;
    ++frames;
    stats.AddFrame(render_end - render_start, present_start - render_end, present_end - present_start);
    const net::Stack* net = NetworkStack();
    stats.Update(present_end, {.write_combining = write_combining ? "ON" : "OFF",
                               .timer_ticks = TimerTicks(),
                               .unexpected_irqs = UnexpectedInterrupts(),
                               .keyboard = keyboard.mode(),
                               .sound = SoundStatus(),
                               .sound_irqs = SoundInterrupts(),
                               .last_key = keyboard.last_code(),
                               .network = NetworkStatus(),
                               .frames_received = net != nullptr ? net->frames_received() : 0,
                               .frames_sent = net != nullptr ? net->frames_sent() : 0});
  }
}

}  // namespace platform
