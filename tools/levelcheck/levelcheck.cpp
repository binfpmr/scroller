// levelcheck [--basic] <level.txt>: prints the level with 'o' on every tile the player can stand on, reachable from
// 'P'. --basic: only walking and plain jumps (no duck-jumps or wall-jumps), the moves a beginner level may need.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "reachability.h"

int main(int argc, char** argv) {
  const bool basic = argc == 3 && std::string(argv[1]) == "--basic";
  if (argc != 2 && !basic) {
    std::cerr << "usage: levelcheck [--basic] <level.txt>\n";
    return 2;
  }
  const char* path = argv[argc - 1];
  std::ifstream file(path, std::ios::binary);
  std::stringstream text;
  text << file.rdbuf();
  if (!file) {
    std::cerr << "cannot read " << path << "\n";
    return 2;
  }
  const std::string data = text.str();
  std::vector<uint8_t> storage(data.size() * 2 + 4096);
  lib::Arena arena(storage.data(), storage.size());
  game::Level level;
  const char* error = nullptr;
  if (!game::ParseLevel(data.data(), data.size(), arena, &level, &error)) {
    std::cerr << path << ": " << error << "\n";
    return 1;
  }
  const levelcheck::Reachability reach =
      levelcheck::Explore(level, 200000, basic ? levelcheck::Moves::kBasic : levelcheck::Moves::kAll);
  std::cout << levelcheck::RenderMap(level, reach) << reach.rest_positions.size() << " resting positions reachable\n";
  bool has_goal = false;
  for (int i = 0; i < level.width * level.height; ++i) {
    has_goal = has_goal || level.tiles[i] == game::Tile::kGoal;
  }
  if (has_goal) {
    const bool reachable = reach.ReachesGoal(level);
    const bool dead_end = reach.HasDeadEnd(level);
    std::cout << "goal " << (reachable ? "reachable" : "NOT reachable") << ", "
              << (dead_end ? "DEAD ENDS (x) present" : "no dead ends") << "\n";
    return reachable && !dead_end ? 0 : 1;
  }
  return 0;
}
