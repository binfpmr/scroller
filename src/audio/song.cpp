#include "audio/song.h"

namespace audio {
namespace {

struct Token {
  const char* begin = nullptr;
  size_t length = 0;
  bool Is(const char* word) const {
    size_t i = 0;
    for (; i < length; ++i) {
      if (word[i] != begin[i]) {
        return false;
      }
    }
    return word[i] == '\0';
  }
};

// Splits a line into whitespace-separated tokens, stopping at a token that starts with '#'.
int Tokenize(const char* begin, const char* end, Token* tokens, int max_tokens) {
  int count = 0;
  const char* p = begin;
  while (p < end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) {
      ++p;
    }
    if (p == end || *p == '#') {
      break;
    }
    const char* start = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\r') {
      ++p;
    }
    if (count == max_tokens) {
      return -1;
    }
    tokens[count++] = {start, static_cast<size_t>(p - start)};
  }
  return count;
}

bool ParseNumber(const Token& t, int max, int* value) {
  if (t.length == 0 || t.length > 5) {
    return false;
  }
  int v = 0;
  for (size_t i = 0; i < t.length; ++i) {
    if (t.begin[i] < '0' || t.begin[i] > '9') {
      return false;
    }
    v = v * 10 + (t.begin[i] - '0');
  }
  if (v > max) {
    return false;
  }
  *value = v;
  return true;
}

bool ParseWave(const Token& t, Wave* wave) {
  if (t.Is("square")) {
    *wave = Wave::kSquare;
  } else if (t.Is("triangle")) {
    *wave = Wave::kTriangle;
  } else if (t.Is("saw")) {
    *wave = Wave::kSaw;
  } else if (t.Is("noise")) {
    *wave = Wave::kNoise;
  } else {
    return false;
  }
  return true;
}

}  // namespace

bool ParseNote(const char* text, size_t length, int* note) {
  if (length != 3) {
    return false;
  }
  constexpr int kSemitones[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
  const char letter = text[0];
  if (letter < 'A' || letter > 'G' || text[2] < '0' || text[2] > '8') {
    return false;
  }
  int n = kSemitones[letter - 'A'];
  if (text[1] == '#') {
    ++n;
  } else if (text[1] == 'b') {
    --n;
  } else if (text[1] != '-') {
    return false;
  }
  *note = (text[2] - '0' + 1) * 12 + n;  // C-4 = 60
  return *note >= 0 && *note < 128;
}

bool Song::Parse(const char* text, size_t length, lib::Arena& arena, const char** error, int* error_line) {
  // Upper bound on rows: the number of lines.
  int max_rows = 1;
  for (size_t i = 0; i < length; ++i) {
    max_rows += text[i] == '\n' ? 1 : 0;
  }

  int line = 0;
  auto fail = [&](const char* message) {
    *error = message;
    *error_line = line;
    return false;
  };

  const char* p = text;
  const char* end = text + length;
  constexpr int kMaxTokens = 24;
  Token t[kMaxTokens];
  while (p < end) {
    const char* line_end = p;
    while (line_end < end && *line_end != '\n') {
      ++line_end;
    }
    ++line;
    const int n = Tokenize(p, line_end, t, kMaxTokens);
    p = line_end < end ? line_end + 1 : end;
    if (n < 0) {
      return fail("too many cells on a line");
    }
    if (n == 0) {
      continue;
    }

    if (channels_ > 0) {  // song rows
      uint8_t* row = cells_ + rows_ * channels_;
      int channel = 0;
      for (int i = 0; i < n; ++i) {
        const Token& c = t[i];
        if (c.Is("|")) {
          continue;
        }
        if (channel == channels_) {
          return fail("more cells than channels");
        }
        int note = 0;
        if (c.Is(".") || c.Is("...")) {
          row[channel] = kContinue;
        } else if (c.Is("-") || c.Is("---")) {
          row[channel] = kRelease;
        } else if (c.Is("x")) {
          row[channel] = kHit;
        } else if (ParseNote(c.begin, c.length, &note)) {
          row[channel] = static_cast<uint8_t>(note);
        } else {
          return fail("bad cell (expected a note like C-4 or F#5, x, ., or ---)");
        }
        ++channel;
      }
      if (channel == 0) {
        continue;  // only bar lines
      }
      if (channel != channels_) {
        return fail("fewer cells than channels");
      }
      ++rows_;
      continue;
    }

    if (t[0].Is("tempo")) {
      if (n != 2 || !ParseNumber(t[1], 1000, &tempo_) || tempo_ < 10) {
        return fail("expected 'tempo <beats per minute>' (10..1000)");
      }
    } else if (t[0].Is("rows_per_beat")) {
      if (n != 2 || !ParseNumber(t[1], 32, &rows_per_beat_) || rows_per_beat_ < 1) {
        return fail("expected 'rows_per_beat <1..32>'");
      }
    } else if (t[0].Is("loop")) {
      if (n != 2 || !(t[1].Is("yes") || t[1].Is("no"))) {
        return fail("expected 'loop yes' or 'loop no'");
      }
      loop_ = t[1].Is("yes");
    } else if (t[0].Is("instrument")) {
      if (n < 3 || instrument_count_ == kMaxInstruments) {
        return fail("expected 'instrument <name> <wave> ...' (at most 8 instruments)");
      }
      Instrument& ins = instruments_[instrument_count_];
      ins = Instrument();
      if (t[1].length > static_cast<size_t>(Instrument::kMaxNameLength)) {
        return fail("instrument name too long");
      }
      for (size_t i = 0; i < t[1].length; ++i) {
        ins.name[i] = t[1].begin[i];
      }
      ins.name[t[1].length] = '\0';
      if (!ParseWave(t[2], &ins.wave)) {
        return fail("unknown wave (square, triangle, saw, noise)");
      }
      for (int i = 3; i < n; i += 2) {
        if (i + 1 >= n) {
          return fail("instrument setting without a value");
        }
        int v = 0;
        const Token& key = t[i];
        const Token& value = t[i + 1];
        if (key.Is("note")) {
          if (!ParseNote(value.begin, value.length, &v)) {
            return fail("bad note for 'note'");
          }
          ins.default_note = static_cast<uint8_t>(v);
        } else if (!ParseNumber(value, 10000, &v)) {
          return fail("instrument setting needs a number");
        } else if (key.Is("duty") && v >= 1 && v <= 99) {
          ins.duty = static_cast<uint8_t>(v);
        } else if (key.Is("attack")) {
          ins.attack_ms = static_cast<uint16_t>(v);
        } else if (key.Is("decay")) {
          ins.decay_ms = static_cast<uint16_t>(v);
        } else if (key.Is("sustain") && v <= 100) {
          ins.sustain = static_cast<uint8_t>(v);
        } else if (key.Is("release")) {
          ins.release_ms = static_cast<uint16_t>(v);
        } else if (key.Is("volume") && v <= 100) {
          ins.volume = static_cast<uint8_t>(v);
        } else {
          return fail("unknown instrument setting or value out of range");
        }
      }
      ++instrument_count_;
    } else if (t[0].Is("channels")) {
      if (n < 2 || n - 1 > kMaxChannels) {
        return fail("expected 'channels <instrument>...' (1..6 channels)");
      }
      for (int i = 1; i < n; ++i) {
        int found = -1;
        for (int k = 0; k < instrument_count_; ++k) {
          if (t[i].Is(instruments_[k].name)) {
            found = k;
          }
        }
        if (found < 0) {
          return fail("channel uses an instrument that is not defined (define instruments first)");
        }
        channel_instrument_[i - 1] = static_cast<uint8_t>(found);
      }
      cells_ = arena.AllocateArray<uint8_t>(static_cast<size_t>(max_rows) * (n - 1));
      if (cells_ == nullptr) {
        return fail("out of memory for the song");
      }
      channels_ = n - 1;
    } else {
      return fail("unknown directive (tempo, rows_per_beat, loop, instrument, channels)");
    }
  }
  if (channels_ == 0 || rows_ == 0) {
    *error = "song has no 'channels' line or no rows";
    *error_line = line;
    return false;
  }
  return true;
}

}  // namespace audio
