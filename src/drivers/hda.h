// Intel High Definition Audio: controller (CORB/RIRB command rings, one output stream) and codec setup (widget graph
// walk from output pins to a DAC). Plays 48 kHz 16-bit stereo from a looping buffer; the caller refills it behind
// the hardware's play position (no interrupts needed).
#pragma once

#include <stdint.h>

#include "drivers/pci.h"
#include "lib/arena.h"

namespace hda {

class Controller {
 public:
  static constexpr uint32_t kSampleRate = 48000;
  static constexpr uint32_t kBufferFrames = 4096;  // stereo frames in the looping buffer (85 ms)

  // Resets the controller, finds a codec with an analog output path, configures it and the stream. Logs what it
  // finds; description() summarises it. False if anything is missing.
  bool Init(const pci::Device& device, lib::Arena& arena);
  // Starts the stream; fill buffer() first.
  void Start();

  int16_t* buffer() { return buffer_; }  // interleaved left/right, kBufferFrames frames
  // Absolute number of frames played since Start (follows the hardware position, including wrap-arounds). Call at
  // least once per buffer length.
  uint64_t FramesPlayed();
  const char* description() const { return description_; }

 private:
  uint32_t Command(uint32_t verb);  // send one codec verb, return the response (0xffffffff on timeout)
  bool SetUpCodec(int codec);

  volatile uint8_t* mmio_ = nullptr;
  uint32_t* corb_ = nullptr;
  uint64_t* rirb_ = nullptr;  // 64-bit entries: response, response_ex
  uint16_t corb_entries_ = 0;
  uint16_t rirb_entries_ = 0;
  uint16_t rirb_read_ = 0;
  bool command_failed_ = false;
  uint32_t stream_offset_ = 0;  // register offset of our output stream descriptor
  int16_t* buffer_ = nullptr;
  uint32_t last_position_ = 0;
  uint64_t wraps_ = 0;
  char description_[64] = "";
};

}  // namespace hda
