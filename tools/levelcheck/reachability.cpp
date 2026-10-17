#include "reachability.h"

#include <deque>
#include <unordered_set>

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
    for (int i = 1; i < kSettleTicks && !p.on_ground; ++i) {
      if (m.wall_jump_at > 0 && i >= m.wall_jump_at) {
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
  std::unordered_set<long long> seen{Key2(start)};
  std::deque<Player> queue{start};
  while (!queue.empty() && result.rest_positions.size() < max_states) {
    const Player p = queue.front();
    queue.pop_front();
    result.rest_positions.push_back(p);
    for (const Move& m : moves) {
      Player next = p;
      if (Apply(next, level, m) && seen.insert(Key2(next)).second) {
        queue.push_back(next);
      }
    }
  }
  return result;
}

std::string RenderMap(const game::Level& level, const Reachability& reach) {
  std::string map;
  for (int ty = 0; ty < level.height; ++ty) {
    for (int tx = 0; tx < level.width; ++tx) {
      if (level.IsSolid(tx, ty)) {
        map += '#';
      } else if (tx == level.start_x && ty == level.start_y) {
        map += 'P';
      } else {
        map += reach.StandsOn(tx, ty + 1) ? 'o' : '.';
      }
    }
    map += '\n';
  }
  return map;
}

}  // namespace levelcheck
