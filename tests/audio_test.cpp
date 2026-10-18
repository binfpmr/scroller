#include <cstring>
#include <string>
#include <vector>

#include "audio/engine.h"
#include "audio/song.h"
#include "test.h"

namespace {

constexpr uint32_t kRate = 48000;

struct Parsed {
  std::vector<uint8_t> storage = std::vector<uint8_t>(1 << 16);
  audio::Song song;
  const char* error = nullptr;
  int line = 0;
  bool ok = false;
  explicit Parsed(const std::string& text) {
    lib::Arena arena(storage.data(), storage.size());
    ok = song.Parse(text.data(), text.size(), arena, &error, &line);
  }
};

int SignChanges(const std::vector<int16_t>& s, size_t from, size_t to) {
  int changes = 0;
  for (size_t i = from + 1; i < to; ++i) {
    changes += (s[i - 1] < 0) != (s[i] < 0) ? 1 : 0;
  }
  return changes;
}

std::vector<int16_t> Render(audio::Engine& e, size_t frames) {
  std::vector<int16_t> out(frames);
  e.Render(out.data(), out.size());
  return out;
}

}  // namespace

TEST(NoteNamesAndFrequencies) {
  int n = 0;
  EXPECT_TRUE(audio::ParseNote("C-4", 3, &n) && n == 60);
  EXPECT_TRUE(audio::ParseNote("A-4", 3, &n) && n == 69);
  EXPECT_TRUE(audio::ParseNote("F#5", 3, &n) && n == 78);
  EXPECT_TRUE(audio::ParseNote("Bb3", 3, &n) && n == 58);
  EXPECT_TRUE(!audio::ParseNote("H-4", 3, &n) && !audio::ParseNote("C4", 2, &n) && !audio::ParseNote("C+4", 3, &n));
  EXPECT_EQ(audio::NoteFrequencyMilliHz(69), 440000u);
  EXPECT_EQ(audio::NoteFrequencyMilliHz(57), 220000u);
}

TEST(SquareA4Is440Hz) {
  Parsed p("tempo 60\nrows_per_beat 1\ninstrument sq square\nchannels sq\nA-4\n.\n");
  EXPECT_TRUE(p.ok);
  audio::Engine e;
  e.Init(kRate);
  e.PlayMusic(&p.song);
  const auto s = Render(e, kRate);
  const int changes = SignChanges(s, kRate / 10, kRate / 10 + kRate / 2);  // half a second, after the attack
  EXPECT_TRUE(changes >= 438 && changes <= 442);                           // 2 per period: 440 Hz
}

TEST(PercussionHitDecaysToSilence) {
  Parsed p("instrument hat noise attack 1 decay 30 sustain 0 volume 50 note C-7\nchannels hat\nx\n");
  EXPECT_TRUE(p.ok);
  audio::Engine e;
  e.Init(kRate);
  e.PlaySound(&p.song);
  const auto s = Render(e, kRate / 10);
  int loud = 0;
  for (size_t i = 0; i < kRate / 100; ++i) {
    loud += s[i] != 0 ? 1 : 0;
  }
  EXPECT_TRUE(loud > 100);  // audible at first
  for (size_t i = kRate / 20; i < s.size(); ++i) {
    if (s[i] != 0) {
      test::Fail(__FILE__, __LINE__, "noise still sounding 50 ms after a 31 ms hit");
      break;
    }
  }
}

TEST(RowTimingFollowsTempo) {
  // 120 BPM, 4 rows per beat: a row is 125 ms = 6000 samples; the second note starts at sample 6000.
  Parsed p("tempo 120\nrows_per_beat 4\ninstrument sq square attack 1 release 1\nchannels sq\n---\nA-4\n");
  EXPECT_TRUE(p.ok);
  audio::Engine e;
  e.Init(kRate);
  e.PlayMusic(&p.song);
  const auto s = Render(e, 7000);
  int first = -1;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != 0) {
      first = static_cast<int>(i);
      break;
    }
  }
  EXPECT_TRUE(first >= 6000 && first <= 6002);
}

TEST(NonLoopingSongEndsWhenItsNotesFade) {
  Parsed blip("instrument sq square decay 20 sustain 0\nchannels sq\nC-5\n");
  audio::Engine e;
  e.Init(kRate);
  e.PlayMusic(&blip.song);
  EXPECT_TRUE(e.music_playing());
  Render(e, kRate / 2);
  EXPECT_TRUE(!e.music_playing());
}

TEST(SongParseErrors) {
  struct Case {
    const char* text;
    int line;
    const char* error;
  };
  const Case cases[] = {
      {"tempo 5\n", 1, "expected 'tempo <beats per minute>' (10..1000)"},
      {"instrument a sine\n", 1, "unknown wave (square, triangle, saw, noise)"},
      {"instrument a square volume 101\n", 1, "unknown instrument setting or value out of range"},
      {"channels a\n", 1, "channel uses an instrument that is not defined (define instruments first)"},
      {"instrument a square\nchannels a a\nC-4\n", 3, "fewer cells than channels"},
      {"instrument a square\nchannels a\nC-4 D-4\n", 3, "more cells than channels"},
      {"instrument a square\nchannels a\nH-4\n", 3, "bad cell (expected a note like C-4 or F#5, x, ., or ---)"},
      {"instrument a square\n", 1, "song has no 'channels' line or no rows"},
  };
  for (const Case& c : cases) {
    Parsed p(c.text);
    EXPECT_TRUE(!p.ok);
    EXPECT_STREQ(p.error ? p.error : "", c.error);
    EXPECT_EQ(p.line, c.line);
  }
}

TEST(BarLinesAndCommentsAreIgnored) {
  Parsed p(
      "# a tune\n"
      "instrument a square\n"
      "instrument b triangle\n"
      "channels a b   # two voices\n"
      "C-4 | E-3\n"
      "|\n"
      "... | ---  # rest\n");
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(p.song.rows(), 2);
  EXPECT_EQ(p.song.cell(0, 1), 52);
  EXPECT_EQ(p.song.cell(1, 0), audio::Song::kContinue);
  EXPECT_EQ(p.song.cell(1, 1), audio::Song::kRelease);
}
