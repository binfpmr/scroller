// render_audio <song.txt> <out.wav> [seconds]: plays a song (music or sound effect) through the game's audio engine
// and writes a 16-bit mono WAV at 48 kHz, to listen to on the development machine.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "audio/engine.h"
#include "audio/song.h"

namespace {

void Put32(std::ofstream& f, uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }
void Put16(std::ofstream& f, uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3 || argc > 4) {
    std::cerr << "usage: render_audio <song.txt> <out.wav> [seconds]\n";
    return 2;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 2;
  }
  const std::string data = text.str();
  std::vector<uint8_t> storage(data.size() * 8 + 4096);
  lib::Arena arena(storage.data(), storage.size());
  audio::Song song;
  const char* error = nullptr;
  int line = 0;
  if (!song.Parse(data.data(), data.size(), arena, &error, &line)) {
    std::cerr << argv[1] << " line " << line << ": " << error << "\n";
    return 1;
  }

  constexpr uint32_t kRate = 48000;
  audio::Engine engine;
  engine.Init(kRate);
  engine.PlayMusic(&song);
  const double song_seconds = 60.0 * song.rows() / (song.tempo() * song.rows_per_beat());
  const double seconds = argc == 4 ? std::atof(argv[3]) : song_seconds + 1.0;
  std::vector<int16_t> samples(static_cast<size_t>(seconds * kRate));
  engine.Render(samples.data(), samples.size());

  std::ofstream out(argv[2], std::ios::binary);
  out.write("RIFF", 4);
  Put32(out, 36 + samples.size() * 2);
  out.write("WAVEfmt ", 8);
  Put32(out, 16);
  Put16(out, 1);  // PCM
  Put16(out, 1);  // mono
  Put32(out, kRate);
  Put32(out, kRate * 2);
  Put16(out, 2);
  Put16(out, 16);
  out.write("data", 4);
  Put32(out, samples.size() * 2);
  out.write(reinterpret_cast<const char*>(samples.data()), samples.size() * 2);
  std::cout << argv[2] << ": " << seconds << " s (song " << song_seconds << " s, " << song.rows() << " rows, "
            << song.channels() << " channels)\n";
  return out ? 0 : 1;
}
