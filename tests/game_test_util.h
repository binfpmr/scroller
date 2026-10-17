// Helpers for game tests: levels from inline text, scripted input.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "game/level.h"
#include "platform/input.h"
#include "test.h"

namespace test {

// Contents of a file under assets/, e.g. ReadAsset("levels/test.txt"). Fails the test if missing.
inline std::string ReadAsset(const char* name) {
  std::ifstream file(std::string(SCROLLER_ASSETS_DIR) + "/" + name, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  if (!file) {
    Fail(__FILE__, __LINE__, "cannot read asset");
  }
  return text.str();
}

// Parses `text` into a level backed by a per-call buffer (kept alive by the returned object).
struct TestLevel {
  std::vector<uint8_t> storage = std::vector<uint8_t>(1 << 16);
  game::Level level;

  explicit TestLevel(const char* text) {
    lib::Arena arena(storage.data(), storage.size());
    const char* error = nullptr;
    if (!game::ParseLevel(text, std::strlen(text), arena, &level, &error)) {
      Fail(__FILE__, __LINE__, error);
    }
  }
};

inline platform::InputState Keys(std::initializer_list<platform::Key> held,
                                 std::initializer_list<platform::Key> pressed = {}) {
  platform::InputState s;
  for (platform::Key k : held) {
    s.held |= platform::InputState::Bit(k);
  }
  for (platform::Key k : pressed) {
    s.pressed |= platform::InputState::Bit(k);
    s.held |= platform::InputState::Bit(k);
  }
  return s;
}

}  // namespace test
