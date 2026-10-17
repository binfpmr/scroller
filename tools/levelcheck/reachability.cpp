#include "reachability.h"

#include <deque>
#include <unordered_map>

namespace levelcheck {
namespace {

using game::Player;
using platform::InputState;
using platform::Key;

constexpr int kSettleTicks = 600;

InputState Input(int dir, bool jump_held, bool jump_pressed, bool down = false) {
  InputState s;
  if (down) {
    s.held |= InputState::Bit(Key::kDown);
  }
  if (dir < 0) {
    s.held |= InputState::Bit(Key::kLeft);
  } else if (dir > 0) {
    s.held |= InputState::Bit(Key::kRight);
  }
  if (jump_held || jump_pressed) {
    s.held |= InputState::Bit(Key::kJump);
  }
  if (jump_pressed) {
    s.pressed |= InputState::Bit(Key::kJump);
  }
  return s;
}

// Lets the player come to rest without input. Returns false if it never settles.
bool Settle(Player& p, const game::Level& level) {
  for (int i = 0; i < kSettleTicks; ++i) {
    if (p.on_ground && p.vx == lib::Fixed()) {
      return true;
    }
    game::UpdatePlayer(p, {}, level);
  }
  return false;
}

struct Move {
  int dir;     // run-up direction
  int run_up;  // ticks of running before the jump (or the whole move for a walk)
  bool jump;
  int hold;                // ticks the jump button stays held
  int air_dir;             // direction held while airborne
  bool duck = false;       // Down held when jumping: duck-jump
  int wall_jump_at = 0;    // if > 0: press jump again this many ticks after the first jump (a wall-jump at a wall)
  int after_wall_dir = 0;  // direction held after the second jump
  int chain_every = 0;     // if > 0: press jump every this many ticks, steering toward the next wall (wall-jump climb)
};

std::vector<Move> AllMoves() {
  std::vector<Move> moves;
  for (int dir : {-1, 1}) {
    for (int run : {4, 10, 20, 40}) {
      moves.push_back({dir, run, false, 0, 0});
    }
  }
  for (int dir : {-1, 0, 1}) {
    for (int run : {0, 6, 14, 30}) {
      if (dir == 0 && run > 0) {
        continue;
      }
      for (int hold : {1, 8, 40}) {
        for (int air_dir : {-1, 0, 1}) {
          moves.push_back({dir, run, true, hold, air_dir});
        }
      }
      for (int air_dir : {-1, 0, 1}) {
        moves.push_back({dir, run, true, 60, air_dir, true});
      }
    }
  }
  // Climb between two walls: keep pressing jump, steering the way the last wall-jump pushed.
  for (int dir : {-1, 1}) {
    for (int run : {0, 10}) {
      for (bool duck : {false, true}) {
        for (int every : {6, 10, 14, 18}) {
          moves.push_back({dir, run, true, 60, dir, duck, 0, 0, every});
        }
      }
    }
  }
  // Jump at a wall, then wall-jump off it after various delays.
  for (int dir : {-1, 1}) {
    for (int run : {0, 14}) {
      for (bool duck : {false, true}) {
        for (int at : {6, 12, 18, 26, 34}) {
          for (int after : {-1, 0, 1}) {
            moves.push_back({dir, run, true, 60, dir, duck, at, after});
          }
        }
      }
    }
  }
  return moves;
}

bool Apply(Player& p, const game::Level& level, const Move& m) {
  for (int i = 0; i < m.run_up; ++i) {
    game::UpdatePlayer(p, Input(m.dir, false, false), level);
  }
  if (m.jump) {
    game::UpdatePlayer(p, Input(m.air_dir, true, true, m.duck), level);
    int steer = m.air_dir;
    for (int i = 1; i < kSettleTicks && !p.on_ground; ++i) {
      if (m.chain_every > 0) {
        if (p.wall_jump_away_dir != 0) {
          steer = p.wall_jump_away_dir;  // after the push ends, keep heading for the opposite wall
        }
        game::UpdatePlayer(p, Input(steer, true, i % m.chain_every == 0), level);
      } else if (m.wall_jump_at > 0 && i >= m.wall_jump_at) {
        game::UpdatePlayer(p, Input(m.after_wall_dir, true, i == m.wall_jump_at), level);
      } else {
        game::UpdatePlayer(p, Input(m.air_dir, i < m.hold, false), level);
      }
    }
  }
  return Settle(p, level);
}

long long Key2(const Player& p) { return (static_cast<long long>(p.pixel_x()) << 32) ^ (p.pixel_y() & 0xffffffff); }

}  // namespace

bool Reachability::Touches(int tx, int ty) const {
  for (const Player& p : rest_positions) {
    if (p.pixel_x() < (tx + 1) * game::kTileSize && p.pixel_x() + Player::kWidth > tx * game::kTileSize &&
        p.pixel_y() < (ty + 1) * game::kTileSize && p.pixel_y() + Player::kHeight > ty * game::kTileSize) {
      return true;
    }
  }
  return false;
}

bool Reachability::ReachesGoal(const game::Level& level) const {
  for (int ty = 0; ty < level.height; ++ty) {
    for (int tx = 0; tx < level.width; ++tx) {
      if (level.At(tx, ty) == game::Tile::kGoal && Touches(tx, ty)) {
        return true;
      }
    }
  }
  return false;
}

bool Reachability::StandsOn(int tx, int ty) const {
  for (const Player& p : rest_positions) {
    const bool feet_on_row = p.pixel_y() + Player::kHeight == ty * game::kTileSize;
    const bool over_column =
        p.pixel_x() < (tx + 1) * game::kTileSize && p.pixel_x() + Player::kWidth > tx * game::kTileSize;
    if (feet_on_row && over_column) {
      return true;
    }
  }
  return false;
}

Reachability Explore(const game::Level& level, size_t max_states) {
  Reachability result;
  Player start;
  start.Spawn(level.start_x, level.start_y);
  if (!Settle(start, level)) {
    return result;
  }
  const std::vector<Move> moves = AllMoves();
  std::unordered_map<long long, int> index{{Key2(start), 0}};
  result.rest_positions.push_back(start);
  result.moves_to.emplace_back();
  for (size_t i = 0; i < result.rest_positions.size() && i < max_states; ++i) {
    const Player p = result.rest_positions[i];
    for (const Move& m : moves) {
      Player next = p;
      if (!Apply(next, level, m)) {
        continue;
      }
      auto [it, inserted] = index.try_emplace(Key2(next), static_cast<int>(result.rest_positions.size()));
      if (inserted) {
        result.rest_positions.push_back(next);
        result.moves_to.emplace_back();
      }
      if (it->second != static_cast<int>(i)) {
        result.moves_to[i].push_back(it->second);
      }
    }
  }
  return result;
}

std::vector<bool> Reachability::CanReachGoal(const game::Level& level) const {
  const size_t n = rest_positions.size();
  bool has_goal = false;
  for (int i = 0; i < level.width * level.height; ++i) {
    has_goal = has_goal || level.tiles[i] == game::Tile::kGoal;
  }
  std::vector<bool> can(n, !has_goal);
  if (!has_goal) {
    return can;
  }
  // Reverse search from every position touching a goal tile.
  std::vector<std::vector<int>> moves_from(n);
  for (size_t i = 0; i < n && i < moves_to.size(); ++i) {
    for (int j : moves_to[i]) {
      moves_from[j].push_back(static_cast<int>(i));
    }
  }
  std::deque<int> queue;
  for (size_t i = 0; i < n; ++i) {
    const Player& p = rest_positions[i];
    for (int ty = p.pixel_y() >> game::kTileShift; ty <= (p.pixel_y() + Player::kHeight - 1) >> game::kTileShift;
         ++ty) {
      for (int tx = p.pixel_x() >> game::kTileShift; tx <= (p.pixel_x() + Player::kWidth - 1) >> game::kTileShift;
           ++tx) {
        if (!can[i] && level.At(tx, ty) == game::Tile::kGoal) {
          can[i] = true;
          queue.push_back(static_cast<int>(i));
        }
      }
    }
  }
  while (!queue.empty()) {
    const int i = queue.front();
    queue.pop_front();
    for (int j : moves_from[i]) {
      if (!can[j]) {
        can[j] = true;
        queue.push_back(j);
      }
    }
  }
  return can;
}

bool Reachability::HasDeadEnd(const game::Level& level) const {
  for (bool ok : CanReachGoal(level)) {
    if (!ok) {
      return true;
    }
  }
  return false;
}

std::string RenderMap(const game::Level& level, const Reachability& reach) {
  const std::vector<bool> can_reach_goal = reach.CanReachGoal(level);
  auto dead_end_at = [&](int tx, int ty) {
    for (size_t i = 0; i < reach.rest_positions.size(); ++i) {
      const Player& p = reach.rest_positions[i];
      const bool feet_on_row = p.pixel_y() + Player::kHeight == (ty + 1) * game::kTileSize;
      const bool over_column =
          p.pixel_x() < (tx + 1) * game::kTileSize && p.pixel_x() + Player::kWidth > tx * game::kTileSize;
      if (feet_on_row && over_column && !can_reach_goal[i]) {
        return true;
      }
    }
    return false;
  };
  std::string map;
  for (int ty = 0; ty < level.height; ++ty) {
    for (int tx = 0; tx < level.width; ++tx) {
      const game::Tile tile = level.At(tx, ty);
      if (tile == game::Tile::kGround) {
        map += '#';
      } else if (tile == game::Tile::kStone) {
        map += 'X';
      } else if (tile == game::Tile::kGoal) {
        map += reach.Touches(tx, ty) ? 'g' : 'G';
      } else if (tx == level.start_x && ty == level.start_y) {
        map += 'P';
      } else {
        map += dead_end_at(tx, ty) ? 'x' : reach.StandsOn(tx, ty + 1) ? 'o' : '.';
      }
    }
    map += '\n';
  }
  return map;
}

}  // namespace levelcheck
