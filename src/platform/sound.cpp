#include "platform/sound.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/interrupts.h"
#include "audio/engine.h"
#include "audio/song.h"
#include "drivers/debug_log.h"
#include "drivers/local_apic.h"
#include "drivers/sb16.h"
#include "game/game.h"
#include "lib/format.h"
#include "lib/mem.h"
#include "platform/interrupt_controllers.h"

namespace platform {
namespace {

constexpr uint8_t kSoundVector = 0x42;
constexpr uint32_t kSb16Rate = 44100;
constexpr uint32_t kHalfFrames = 1024;  // 23 ms at 44.1 kHz

audio::Engine g_engine;
audio::Song g_music;
audio::Song g_jump;
audio::Song g_land;
audio::Song g_wall_jump;
audio::Song g_goal;
bool g_have_effects = false;

int16_t* g_buffer = nullptr;
int g_next_half = 0;
volatile uint64_t g_interrupts = 0;
char g_status[48] = "NONE";

void OnSb16Interrupt(x86::InterruptFrame&) {
  sb16::AcknowledgeInterrupt();
  g_engine.Render(g_buffer + g_next_half * kHalfFrames, kHalfFrames);  // the half that just finished playing
  g_next_half ^= 1;
  g_interrupts = g_interrupts + 1;
  lapic::EndOfInterrupt();
}

bool LoadSong(const lib::Pack& pack, const char* name, lib::Arena& arena, audio::Song* song) {
  lib::Blob blob;
  const char* error = nullptr;
  int line = 0;
  if (!pack.Find(name, &blob)) {
    Log("sound: %s not in the pack\n", name);
    return false;
  }
  if (!song->Parse(blob.data, blob.size, arena, &error, &line)) {
    Log("sound: %s line %d: %s\n", name, line, error);
    return false;
  }
  return true;
}

}  // namespace

bool InitSound(const BootInfo& boot, const lib::Pack& pack, lib::Arena& arena) {
  sb16::Config config;
  if (!boot.owns_machine || boot.isa_dma_buffer == 0 || !sb16::Probe(&config)) {
    Log("sound: no device\n");
    return false;
  }
  g_engine.Init(kSb16Rate);
  const bool have_music = LoadSong(pack, "music/minuet.txt", arena, &g_music);
  g_have_effects =
      LoadSong(pack, "sounds/jump.txt", arena, &g_jump) && LoadSong(pack, "sounds/land.txt", arena, &g_land) &&
      LoadSong(pack, "sounds/walljump.txt", arena, &g_wall_jump) && LoadSong(pack, "sounds/goal.txt", arena, &g_goal);
  if (have_music) {
    g_engine.PlayMusic(&g_music);
  }

  g_buffer = reinterpret_cast<int16_t*>(boot.isa_dma_buffer);
  g_engine.Render(g_buffer, 2 * kHalfFrames);
  g_next_half = 0;
  if (!RouteIsaIrq(config.irq, kSoundVector, &OnSb16Interrupt) ||
      !sb16::Start(config, boot.isa_dma_buffer, kHalfFrames, kSb16Rate)) {
    MaskIsaIrq(config.irq);
    return false;
  }
  lib::Format(g_status, sizeof(g_status), "SB16 %u HZ IRQ %u DMA %u", kSb16Rate, config.irq, config.dma16);
  return true;
}

void PlaySoundEvents(uint8_t events) {
  if (!g_have_effects || events == 0) {
    return;
  }
  x86::DisableInterrupts();  // the engine also runs in the interrupt handler
  if (events & game::Game::kEventGoal) {
    g_engine.PlaySound(&g_goal);
  } else if (events & game::kEventWallJump) {
    g_engine.PlaySound(&g_wall_jump);
  } else if (events & game::kEventJump) {
    g_engine.PlaySound(&g_jump);
  }
  if (events & game::kEventLand) {
    g_engine.PlaySound(&g_land);
  }
  x86::EnableInterrupts();
}

const char* SoundStatus() { return g_status; }
uint64_t SoundInterrupts() { return g_interrupts; }

}  // namespace platform
