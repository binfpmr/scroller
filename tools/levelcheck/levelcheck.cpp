// levelcheck <level.txt>: prints the level with 'o' on every tile the player can stand on, reachable from 'P'.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "reachability.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: levelcheck <level.txt>\n";
    return 2;
  }
  std::ifstream file(argv[1], std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  if (!file) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 2;
  }
  const std::string data = text.str();
  std::vector<uint8_t> storage(data.size() * 2 + 4096);
  lib::Arena arena(storage.data(), storage.size());
  game::Level level;
  const char* error = nullptr;
  if (!game::ParseLevel(data.data(), data.size(), arena, &level, &error)) {
    std::cerr << argv[1] << ": " << error << "\n";
    return 1;
  }
  const levelcheck::Reachability reach = levelcheck::Explore(level);
  std::cout << levelcheck::RenderMap(level, reach) << reach.rest_positions.size() << " resting positions reachable\n";
  return 0;
}
