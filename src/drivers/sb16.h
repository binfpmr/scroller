// Sound Blaster 16 (QEMU -device sb16; ISA, so not on modern hardware): 16-bit mono playback through ISA DMA in
// auto-init mode over a buffer of two halves, with an interrupt after each half.
#pragma once

#include <stdint.h>

namespace sb16 {

struct Config {
  uint16_t version = 0;  // DSP version, 0x0405 for QEMU's SB16
  uint8_t irq = 0;
  uint8_t dma16 = 0;
};

// Resets the DSP at port 0x220 and reads its IRQ and 16-bit DMA channel from the mixer. False if there is no SB16.
bool Probe(Config* config);

// Starts playback of `buffer_physical` (below 16 MiB, must not cross a 128 KiB boundary) holding 2 * `half_frames`
// signed 16-bit mono samples, looping forever. Fill both halves first.
bool Start(const Config& config, uint64_t buffer_physical, uint32_t half_frames, uint32_t sample_rate);

// Ends auto-init playback (before a hot reload).
void Stop();

// In the interrupt handler: acknowledges the DSP interrupt.
void AcknowledgeInterrupt();

}  // namespace sb16
