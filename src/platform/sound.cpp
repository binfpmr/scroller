#include "platform/sound.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/interrupts.h"
#include "audio/engine.h"
#include "audio/song.h"
#include "drivers/debug_log.h"
#include "drivers/hda.h"
#include "drivers/local_apic.h"
#include "drivers/pci.h"
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
audio::Song g_duck;
audio::Song g_wall_jump;
audio::Song g_goal;
bool g_have_effects = false;
volatile uint64_t g_interrupts = 0;  // buffer refills
char g_status[48] = "NONE";
constexpr int kMusicVolume = 40;  // music sits below the effects
int g_volume = 20;
bool g_muted = false;
bool g_music_on = false;  // off by default; N switches it on

void ApplyVolume() {
  x86::DisableInterrupts();  // the engine also runs in interrupt handlers
  g_engine.SetMasterVolume(g_muted ? 0 : g_volume);
  g_engine.SetMusicVolume(g_music_on ? kMusicVolume : 0);
  x86::EnableInterrupts();
}

// HDA: the timer interrupt keeps the looping buffer kHdaLeadFrames ahead of the play position.
constexpr uint32_t kHdaLeadFrames = 2048;  // ~43 ms
hda::Controller g_hda;
uint64_t g_hda_written = 0;  // absolute frames written

void RefillHda() {
  const uint64_t target = g_hda.FramesPlayed() + kHdaLeadFrames;
  int16_t mono[256];
  while (g_hda_written < target) {
    const uint32_t offset = static_cast<uint32_t>(g_hda_written % hda::Controller::kBufferFrames);
    uint32_t frames = static_cast<uint32_t>(target - g_hda_written);
    frames = lib::Min(frames, hda::Controller::kBufferFrames - offset);
    frames = lib::Min<uint32_t>(frames, 256);
    g_engine.Render(mono, frames);
    int16_t* out = g_hda.buffer() + 2 * offset;
    for (uint32_t i = 0; i < frames; ++i) {
      out[2 * i] = mono[i];
      out[2 * i + 1] = mono[i];
    }
    g_hda_written += frames;
  }
  g_interrupts = g_interrupts + 1;
}

// Sound Blaster 16: refilled from its own interrupt, half a buffer at a time.
int16_t* g_buffer = nullptr;
int g_next_half = 0;

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

void LoadSongs(const lib::Pack& pack, lib::Arena& arena) {
  g_engine.SetMusicVolume(g_music_on ? kMusicVolume : 0);  // the music runs silently while off
  g_engine.SetMasterVolume(g_muted ? 0 : g_volume);
  const bool have_music = LoadSong(pack, "music/minuet.txt", arena, &g_music);
  g_have_effects =
      LoadSong(pack, "sounds/jump.txt", arena, &g_jump) && LoadSong(pack, "sounds/land.txt", arena, &g_land) &&
      LoadSong(pack, "sounds/duck.txt", arena, &g_duck) && LoadSong(pack, "sounds/walljump.txt", arena, &g_wall_jump) &&
      LoadSong(pack, "sounds/goal.txt", arena, &g_goal);
  if (have_music) {
    g_engine.PlayMusic(&g_music);
  }
}

bool StartHda(lib::Pack const& pack, lib::Arena& arena) {
  pci::Device device;
  if (!pci::FindByClass(0x04, 0x03, &device) || !g_hda.Init(device, arena)) {
    return false;
  }
  g_engine.Init(hda::Controller::kSampleRate);
  LoadSongs(pack, arena);
  x86::DisableInterrupts();
  g_hda_written = 0;
  g_hda.Start();
  RefillHda();
  SetTimerCallback(&RefillHda);
  x86::EnableInterrupts();
  lib::Format(g_status, sizeof(g_status), "%s", g_hda.description());
  return true;
}

bool StartSb16(const BootInfo& boot, const lib::Pack& pack, lib::Arena& arena) {
  sb16::Config config;
  if (boot.isa_dma_buffer == 0 || !sb16::Probe(&config)) {
    return false;
  }
  g_engine.Init(kSb16Rate);
  LoadSongs(pack, arena);
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

}  // namespace

bool InitSound(const BootInfo& boot, const lib::Pack& pack, lib::Arena& arena) {
  if (!boot.owns_machine) {
    return false;
  }
  if (StartHda(pack, arena) || StartSb16(boot, pack, arena)) {
    return true;
  }
  Log("sound: no device\n");
  return false;
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
  } else if (events & game::kEventDuck) {
    g_engine.PlaySound(&g_duck);
  }
  x86::EnableInterrupts();
}

void SetVolume(int percent) {
  g_volume = lib::Clamp(percent, 0, 100);
  ApplyVolume();
}
int Volume() { return g_volume; }
void SetMuted(bool muted) {
  g_muted = muted;
  ApplyVolume();
}
bool Muted() { return g_muted; }
void SetMusicOn(bool on) {
  g_music_on = on;
  ApplyVolume();
}
bool MusicOn() { return g_music_on; }

void PauseSound() {
  if (g_hda.buffer() != nullptr) {
    lib::Fill(g_hda.buffer(), 0, hda::Controller::kBufferFrames * 4);
  }
  if (g_buffer != nullptr) {
    lib::Fill(g_buffer, 0, 2 * kHalfFrames * sizeof(int16_t));
  }
}

void ResumeSound() {
  if (g_hda.buffer() != nullptr) {
    // The position counter missed wrap-arounds meanwhile; carry on from wherever the hardware is now.
    g_hda_written = g_hda.FramesPlayed();
  }
}

const char* SoundStatus() { return g_status; }
uint64_t SoundInterrupts() { return g_interrupts; }

}  // namespace platform
