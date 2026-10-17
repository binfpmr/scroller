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
  std::vector<std::vector<int>> moves_to;    // for each position, the positions one move leads to

  // Per position: can the goal still be reached from it? (All true if the level has no goal.)
  std::vector<bool> CanReachGoal(const game::Level& level) const;
  // True if the player can rest at a spot from which the goal is no longer reachable.
  bool HasDeadEnd(const game::Level& level) const;

  // True if some resting position has its feet on top of tile row `ty` with the hitbox overlapping column `tx`.
  bool StandsOn(int tx, int ty) const;
  // True if some resting position overlaps tile (tx, ty).
  bool Touches(int tx, int ty) const;
  // True if some goal tile ('G') is touched by a resting position (the player can stand at the flag).
  bool ReachesGoal(const game::Level& level) const;
};

Reachability Explore(const game::Level& level, size_t max_states = 200000);

// The level as text with 'o' in every empty tile the player can stand in, 'x' where they can stand but never reach the
// goal from (dead end), and 'g' for reachable goals.
std::string RenderMap(const game::Level& level, const Reachability& reach);

}  // namespace levelcheck
