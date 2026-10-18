// Sound output: the audio engine fed to whatever device is present (Intel HDA, else Sound Blaster 16), music started
// from the asset pack, and game events turned into sound effects.
#pragma once

#include <stdint.h>

#include "lib/arena.h"
#include "lib/pack.h"
#include "platform/boot_info.h"

namespace platform {

// Needs interrupts set up (the device refills its buffer from an interrupt). Loads the level tunes (music/) and the sound
// effects from the pack; starts the music. Returns false (game stays silent) if there is no usable device.
bool InitSound(const BootInfo& boot, const lib::Pack& pack, lib::Arena& arena);

// Starts the tune of a level (0-based) from its beginning; the levels take turns through the tunes.
void PlayLevelMusic(int level_index);

// Plays the effects for game::Game::TakeEvents() bits.
void PlaySoundEvents(uint8_t events);

// For the stats overlay, e.g. "SB16 44100 HZ IRQ 5 DMA 5" or "NONE".
// Master volume in percent (0..100; starts at 50). Muting keeps the volume to restore.
void SetVolume(int percent);
int Volume();
void SetMuted(bool muted);
bool Muted();
// Background music on or off (on at start); the effects stay.
void SetMusicOn(bool on);
bool MusicOn();

// For the debugger: while the machine is stopped (interrupts off, nothing refills the looping buffer) play silence,
// then carry on from the hardware's current position. Interrupts must be off for both.
void PauseSound();
void ResumeSound();
// Stops the device's DMA for good (before a hot reload). Leaves interrupts off.
void StopSound();

const char* SoundStatus();
uint64_t SoundInterrupts();

}  // namespace platform
