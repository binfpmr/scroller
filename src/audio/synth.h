// Chiptune-style synthesizer voices: square (variable duty), triangle, saw and noise oscillators with an
// attack/decay/sustain/release envelope. Integer arithmetic only, at any sample rate.
#pragma once

#include <stdint.h>

namespace audio {

enum class Wave : uint8_t {
  kSquare,
  kTriangle,
  kSaw,
  kNoise,
};

struct Instrument {
  static constexpr int kMaxNameLength = 15;

  char name[kMaxNameLength + 1] = "";
  Wave wave = Wave::kSquare;
  uint8_t duty = 50;       // square: percent of the period that is high
  uint16_t attack_ms = 2;  // envelope
  uint16_t decay_ms = 0;
  uint8_t sustain = 100;  // percent of full level held until release
  uint16_t release_ms = 30;
  uint8_t volume = 60;        // percent
  uint8_t default_note = 60;  // note played by an 'x' (percussion hit); C-4
};

// MIDI note number (C-4 = 60, A-4 = 69 = 440 Hz) to frequency in millihertz.
uint32_t NoteFrequencyMilliHz(int note);

class Voice {
 public:
  void Start(const Instrument& instrument, int note, uint32_t sample_rate);
  void Release();
  void Stop() { stage_ = Stage::kOff; }
  bool active() const { return stage_ != Stage::kOff; }

  // Next sample, roughly -32768..32767 times the instrument volume.
  int32_t Next();

 private:
  enum class Stage : uint8_t { kOff, kAttack, kDecay, kSustain, kRelease };

  static uint32_t StepsFor(uint32_t ms, uint32_t sample_rate);

  const Instrument* instrument_ = nullptr;
  Stage stage_ = Stage::kOff;
  uint32_t phase_ = 0;
  uint32_t phase_step_ = 0;
  uint32_t duty_threshold_ = 0;
  uint16_t lfsr_ = 1;
  int32_t noise_value_ = 0;
  // Envelope level in 1/65536 of full scale, with per-sample deltas for the current stage.
  int32_t level_ = 0;
  int32_t sustain_level_ = 0;
  int32_t attack_delta_ = 0;
  int32_t decay_delta_ = 0;
  int32_t release_delta_ = 0;
};

}  // namespace audio
