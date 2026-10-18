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
    int32_t mix = 0;
    for (Voice& v : music_voices_) {
      mix += v.Next();
    }
    for (auto& slot : sound_voices_) {
      for (Voice& v : slot) {
        mix += v.Next();
      }
    }
    mix /= 2;  // headroom: several full-volume voices would clip
    out[i] = static_cast<int16_t>(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
  }
}

}  // namespace audio
