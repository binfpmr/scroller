// Host-only level analysis: which standing positions can the player reach from the start, using the real physics?
// Breadth-first search over resting positions (on the ground, horizontal speed 0) with a fixed set of moves:
// walks and jumps with different run-ups, jump-button hold times and air steering.
#pragma once

#include <string>
#include <vector>

#include "game/level.h"
#include "game/player.h"

namespace levelcheck {

struct Reachability {
  std::vector<game::Player> rest_positions;  // every distinct resting position found (pixel precision)

  // True if some resting position has its feet on top of tile row `ty` with the hitbox overlapping column `tx`.
  bool StandsOn(int tx, int ty) const;
};

Reachability Explore(const game::Level& level, size_t max_states = 200000);

// The level as text with 'o' in every empty tile the player can stand in.
std::string RenderMap(const game::Level& level, const Reachability& reach);

}  // namespace levelcheck
