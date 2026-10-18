// Plays one music song (looping or not) plus short sound effects and mixes them into 16-bit mono samples.
// Not thread safe: when Render runs in an interrupt handler, call the other methods with interrupts disabled.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio/song.h"
#include "audio/synth.h"

namespace audio {

class Engine {
 public:
  static constexpr int kSoundSlots = 2;  // sound effects playing at once (each up to 2 channels)

  void Init(uint32_t sample_rate);
  uint32_t sample_rate() const { return sample_rate_; }

  // Starts `song` from the beginning; null stops the music.
  void PlayMusic(const Song* song);
  // Starts a sound effect, replacing the oldest one if all slots are busy.
  void PlaySound(const Song* song);

  // Produces `frames` mono samples.
  void Render(int16_t* out, size_t frames);

  bool music_playing() const { return music_.song != nullptr; }

  // Volumes in percent (0..100). Music is relative to the effects; master scales everything.
  void SetMusicVolume(int percent) { music_volume_ = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
  void SetMasterVolume(int percent) { master_volume_ = percent < 0 ? 0 : percent > 100 ? 100 : percent; }
  int music_volume() const { return music_volume_; }
  int master_volume() const { return master_volume_; }

 private:
  struct Track {
    const Song* song = nullptr;
    Voice* voices = nullptr;
    int voice_count = 0;
    int row = 0;
    uint64_t row_samples_fp = 0;  // samples per row, 24.8 fixed point
    uint64_t until_next_row_fp = 0;
    uint32_t started = 0;  // for replacing the oldest sound
  };

  void Start(Track& track, const Song* song);
  void Advance(Track& track);  // one sample of sequencing
  void PlayRow(Track& track);

  uint32_t sample_rate_ = 48000;
  Voice music_voices_[Song::kMaxChannels];
  Voice sound_voices_[kSoundSlots][2];
  Track music_;
  Track sounds_[kSoundSlots];
  uint32_t sound_counter_ = 0;
  int music_volume_ = 100;
  int master_volume_ = 100;
};

}  // namespace audio
