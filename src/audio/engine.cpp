#include "audio/engine.h"

namespace audio {

void Engine::Init(uint32_t sample_rate) {
  sample_rate_ = sample_rate;
  music_.voices = music_voices_;
  music_.voice_count = Song::kMaxChannels;
  for (int i = 0; i < kSoundSlots; ++i) {
    sounds_[i].voices = sound_voices_[i];
    sounds_[i].voice_count = 2;
  }
}

void Engine::Start(Track& track, const Song* song) {
  for (int i = 0; i < track.voice_count; ++i) {
    track.voices[i].Stop();
  }
  track.song = song;
  track.row = 0;
  if (song == nullptr) {
    return;
  }
  track.row_samples_fp = (uint64_t{sample_rate_} * 60 * 256) / (song->tempo() * song->rows_per_beat());
  track.until_next_row_fp = 0;  // first row plays immediately
}

void Engine::PlayMusic(const Song* song) { Start(music_, song); }

void Engine::PlaySound(const Song* song) {
  Track* slot = &sounds_[0];
  for (Track& t : sounds_) {
    if (t.song == nullptr) {
      slot = &t;
      break;
    }
    if (t.started < slot->started) {
      slot = &t;
    }
  }
  Start(*slot, song);
  slot->started = ++sound_counter_;
}

void Engine::PlayRow(Track& track) {
  const Song& song = *track.song;
  const int channels = song.channels() < track.voice_count ? song.channels() : track.voice_count;
  for (int c = 0; c < channels; ++c) {
    const uint8_t cell = song.cell(track.row, c);
    const Instrument& instrument = song.instrument(c);
    if (cell == Song::kRelease) {
      track.voices[c].Release();
    } else if (cell == Song::kHit) {
      track.voices[c].Start(instrument, instrument.default_note, sample_rate_);
    } else if (cell != Song::kContinue) {
      track.voices[c].Start(instrument, cell, sample_rate_);
    }
  }
}

void Engine::Advance(Track& track) {
  if (track.song == nullptr) {
    return;
  }
  if (track.until_next_row_fp >= 256) {
    track.until_next_row_fp -= 256;
    return;
  }
  if (track.row == track.song->rows()) {
    if (!track.song->loop()) {
      // Let the last notes ring out, then free the track.
      bool any = false;
      for (int i = 0; i < track.voice_count; ++i) {
        any = any || track.voices[i].active();
      }
      if (!any) {
        track.song = nullptr;
      }
      return;
    }
    track.row = 0;
  }
  PlayRow(track);
  ++track.row;
  track.until_next_row_fp += track.row_samples_fp - 256;
}

void Engine::Render(int16_t* out, size_t frames) {
  for (size_t i = 0; i < frames; ++i) {
    Advance(music_);
    for (Track& t : sounds_) {
      Advance(t);
    }
    int32_t music = 0;
    for (Voice& v : music_voices_) {
      music += v.Next();
    }
    int32_t effects = 0;
    for (auto& slot : sound_voices_) {
      for (Voice& v : slot) {
        effects += v.Next();
      }
    }
    int32_t mix = music * music_volume_ / 100 + effects;
    // Rounded: at low volume the samples are small and truncation would bias them. /2 headroom: voices add up.
    mix = static_cast<int32_t>((static_cast<int64_t>(mix) * master_gain_ + (1 << 16)) >> 17);
    out[i] = static_cast<int16_t>(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
  }
}

void Engine::SetMasterVolume(int percent) {
  master_volume_ = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  // 0.5 dB per percent: gain = 10^(-(100 - percent) * 0.5 / 20), stepped down from full in 16.16 fixed point.
  constexpr int32_t kStep = 61870;  // 10^(-0.025) * 65536
  int32_t gain = 65536;
  for (int p = 100; p > master_volume_; --p) {
    gain = static_cast<int32_t>((static_cast<int64_t>(gain) * kStep) >> 16);
  }
  master_gain_ = master_volume_ == 0 ? 0 : gain;
}

}  // namespace audio
