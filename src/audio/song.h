// Plain-text songs (music and sound effects), a minimal tracker format:
//
//   # comment
//   tempo 120                 beats per minute
//   rows_per_beat 4           rows are sixteenth notes here
//   loop yes                  start over at the end (sound effects: no)
//   instrument lead square duty 25 attack 2 decay 60 sustain 50 release 80 volume 60
//   instrument hat noise attack 1 decay 40 sustain 0 volume 30 note C-7
//   channels lead hat         one column per channel, with its instrument
//   E-4  x                    then one row per line; each cell:
//   ...  .                      C-4 D#5 Bb3  play a note ('-' natural, '#' sharp, 'b' flat)
//   ---  .                      x            play the instrument's own note (percussion)
//                               . or ...     keep going        --- or -   release
//
// Waves: square, triangle, saw, noise. Instrument keys: duty (square, percent), attack/decay/release (ms), sustain
// (percent), volume (percent), note (for 'x'). A '|' cell is ignored (bar lines); '#' starts a comment.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio/synth.h"
#include "lib/arena.h"

namespace audio {

class Song {
 public:
  static constexpr int kMaxInstruments = 8;
  static constexpr int kMaxChannels = 6;
  static constexpr uint8_t kContinue = 0xff;
  static constexpr uint8_t kRelease = 0xfe;
  static constexpr uint8_t kHit = 0xfd;  // 'x': the instrument's default note

  // On error returns false with a static message and the 1-based line.
  bool Parse(const char* text, size_t length, lib::Arena& arena, const char** error, int* error_line);

  int tempo() const { return tempo_; }
  int rows_per_beat() const { return rows_per_beat_; }
  bool loop() const { return loop_; }
  int channels() const { return channels_; }
  int rows() const { return rows_; }
  const Instrument& instrument(int channel) const { return instruments_[channel_instrument_[channel]]; }
  // Note number, kContinue, kRelease or kHit.
  uint8_t cell(int row, int channel) const { return cells_[row * channels_ + channel]; }

 private:
  int tempo_ = 120;
  int rows_per_beat_ = 4;
  bool loop_ = false;
  Instrument instruments_[kMaxInstruments];
  int instrument_count_ = 0;
  uint8_t channel_instrument_[kMaxChannels] = {};
  int channels_ = 0;
  int rows_ = 0;
  uint8_t* cells_ = nullptr;
};

// Parses a note name like "C-4", "F#5", "Bb3" into a MIDI note number; false if it is not one.
bool ParseNote(const char* text, size_t length, int* note);

}  // namespace audio
