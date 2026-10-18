#include "audio/synth.h"

namespace audio {
namespace {

constexpr int32_t kFullLevel = 1 << 16;

// Octave 8 (C-8 .. B-8) in millihertz; lower octaves halve.
constexpr uint32_t kOctave8MilliHz[12] = {4186009, 4434922, 4698636, 4978032, 5274041, 5587652,
                                          5919911, 6271927, 6644875, 7040000, 7458620, 7902133};

}  // namespace

uint32_t NoteFrequencyMilliHz(int note) {
  if (note < 0) {
    note = 0;
  }
  if (note > 119) {
    note = 119;
  }
  const int octave = note / 12 - 1;  // MIDI: note 60 is C-4
  return kOctave8MilliHz[note % 12] >> (8 - octave);
}

uint32_t Voice::StepsFor(uint32_t ms, uint32_t sample_rate) {
  const uint32_t steps = ms * sample_rate / 1000;
  return steps > 0 ? steps : 1;
}

void Voice::Start(const Instrument& instrument, int note, uint32_t sample_rate) {
  instrument_ = &instrument;
  uint32_t frequency = NoteFrequencyMilliHz(note);
  if (instrument.wave == Wave::kNoise) {
    frequency *= 8;  // the noise generator steps once per period: higher notes give brighter noise
  }
  phase_step_ = static_cast<uint32_t>((static_cast<uint64_t>(frequency) << 32) / (uint64_t{sample_rate} * 1000));
  duty_threshold_ = static_cast<uint32_t>((uint64_t{instrument.duty} << 32) / 100);
  phase_ = 0;
  sustain_level_ = kFullLevel * instrument.sustain / 100;
  attack_delta_ = kFullLevel / static_cast<int32_t>(StepsFor(instrument.attack_ms, sample_rate)) + 1;
  decay_delta_ = (kFullLevel - sustain_level_) / static_cast<int32_t>(StepsFor(instrument.decay_ms, sample_rate)) + 1;
  release_delta_ = kFullLevel / static_cast<int32_t>(StepsFor(instrument.release_ms, sample_rate)) + 1;
  level_ = 0;
  stage_ = Stage::kAttack;
}

void Voice::Release() {
  if (stage_ != Stage::kOff) {
    stage_ = Stage::kRelease;
  }
}

int32_t Voice::Next() {
  switch (stage_) {
    case Stage::kOff:
      return 0;
    case Stage::kAttack:
      level_ += attack_delta_;
      if (level_ >= kFullLevel) {
        level_ = kFullLevel;
        stage_ = Stage::kDecay;
      }
      break;
    case Stage::kDecay:
      level_ -= decay_delta_;
      if (level_ <= sustain_level_) {
        level_ = sustain_level_;
        stage_ = Stage::kSustain;
      }
      break;
    case Stage::kSustain:
      if (level_ == 0) {
        stage_ = Stage::kOff;  // e.g. a percussion hit with sustain 0 is over after its decay
        return 0;
      }
      break;
    case Stage::kRelease:
      level_ -= release_delta_;
      if (level_ <= 0) {
        level_ = 0;
        stage_ = Stage::kOff;
        return 0;
      }
      break;
  }

  int32_t wave = 0;
  const uint32_t previous = phase_;
  phase_ += phase_step_;
  switch (instrument_->wave) {
    case Wave::kSquare:
      wave = phase_ < duty_threshold_ ? 32767 : -32767;
      break;
    case Wave::kTriangle: {
      const int32_t ramp = static_cast<int32_t>(phase_ >> 15);  // 0..131071
      wave = (ramp < 65536 ? ramp : 131071 - ramp) - 32768;
      break;
    }
    case Wave::kSaw:
      wave = static_cast<int32_t>(phase_ >> 16) - 32768;
      break;
    case Wave::kNoise:
      if (phase_ < previous) {  // one period passed: step the 15-bit LFSR (as in the NES noise channel)
        const uint16_t bit = static_cast<uint16_t>((lfsr_ ^ (lfsr_ >> 1)) & 1);
        lfsr_ = static_cast<uint16_t>((lfsr_ >> 1) | (bit << 14));
        noise_value_ = (lfsr_ & 1) != 0 ? 32767 : -32767;
      }
      wave = noise_value_;
      break;
  }
  return static_cast<int32_t>((static_cast<int64_t>(wave) * level_ >> 16) * instrument_->volume / 100);
}

}  // namespace audio
