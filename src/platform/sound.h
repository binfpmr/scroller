// Sound output: the audio engine fed to whatever device is present (Sound Blaster 16 for now), music started from
// the asset pack, and game events turned into sound effects.
#pragma once

#include <stdint.h>

#include "lib/arena.h"
#include "lib/pack.h"
#include "platform/boot_info.h"

namespace platform {

// Needs interrupts set up (the device refills its buffer from an interrupt). Loads music/minuet.txt and the sound
// effects from the pack; starts the music. Returns false (game stays silent) if there is no usable device.
bool InitSound(const BootInfo& boot, const lib::Pack& pack, lib::Arena& arena);

// Plays the effects for game::Game::TakeEvents() bits.
void PlaySoundEvents(uint8_t events);

// For the stats overlay, e.g. "SB16 44100 HZ IRQ 5 DMA 5" or "NONE".
const char* SoundStatus();
uint64_t SoundInterrupts();

}  // namespace platform
