#include "game/game.h"

#include "game/render.h"
#include "lib/format.h"

namespace game {
namespace {

// Looks up `name`; on failure formats an error into `message`.
bool FindFrame(const gfx::SpriteSheet* sheet, const char* sheet_name, const char* name, const gfx::Sprite** out,
               char* message, size_t message_size) {
  *out = sheet != nullptr ? sheet->Find(name) : nullptr;
  if (*out == nullptr) {
    lib::Format(message, message_size, "%s has no frame '%s'", sheet_name, name);
    return false;
  }
  return true;
}

}  // namespace

bool Game::Init(lib::Arena& arena, const char* level_text, size_t level_length, const GameAssets& assets,
                const char** error) {
  static char message[64];
  const gfx::SpriteSheet* p = assets.player;
  const gfx::SpriteSheet* t = assets.tiles;
  const char* pn = "sprites/player.txt";
  const char* tn = "sprites/tiles.txt";
  const size_t ms = sizeof(message);
  const bool frames_ok =
      FindFrame(p, pn, "idle", &frames_.idle, message, ms) && FindFrame(p, pn, "blink", &frames_.blink, message, ms) &&
      FindFrame(p, pn, "run1", &frames_.run[0], message, ms) &&
      FindFrame(p, pn, "run2", &frames_.run[1], message, ms) &&
      FindFrame(p, pn, "run3", &frames_.run[2], message, ms) &&
      FindFrame(p, pn, "run4", &frames_.run[3], message, ms) && FindFrame(p, pn, "jump", &frames_.jump, message, ms) &&
      FindFrame(p, pn, "fall", &frames_.fall, message, ms) && FindFrame(p, pn, "duck", &frames_.duck, message, ms) &&
      FindFrame(t, tn, "ground_top", &frames_.ground_top, message, ms) &&
      FindFrame(t, tn, "ground", &frames_.ground, message, ms) &&
      FindFrame(t, tn, "stone", &frames_.stone, message, ms) && FindFrame(t, tn, "flag", &frames_.flag, message, ms);
  if (!frames_ok) {
    *error = message;
    return false;
  }
  if (!ParseLevel(level_text, level_length, arena, &level_, error)) {
    return false;
  }
  Restart();
  return true;
}

void Game::Restart() {
  player_ = Player();
  player_.Spawn(level_.start_x, level_.start_y);
  camera_.CenterOn(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2,
                   level_.pixel_width(), level_.pixel_height());
  win_ticks_ = 0;
}

bool Game::TouchesGoal() const {
  const int left = player_.pixel_x() >> kTileShift;
  const int right = (player_.pixel_x() + Player::kWidth - 1) >> kTileShift;
  const int top = player_.pixel_y() >> kTileShift;
  const int bottom = (player_.pixel_y() + Player::kHeight - 1) >> kTileShift;
  for (int ty = top; ty <= bottom; ++ty) {
    for (int tx = left; tx <= right; ++tx) {
      if (level_.At(tx, ty) == Tile::kGoal) {
        return true;
      }
    }
  }
  return false;
}

void Game::Update(const platform::InputState& input) {
  ++ticks_;
  if (win_ticks_ > 0) {
    UpdatePlayer(player_, {}, level_);  // the player comes to rest; input is ignored
    if (--win_ticks_ == 0) {
      Restart();
    }
    return;
  }
  UpdatePlayer(player_, input, level_);
  camera_.Follow(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2, level_.pixel_width(),
                 level_.pixel_height());
  if (TouchesGoal()) {
    win_ticks_ = kWinTicks;
  }
}

void Game::Render(gfx::Surface& screen) const {
  RenderWorld(screen, level_, camera_, player_, frames_, ticks_);
  if (won()) {
    DrawBanner(screen, "WELL DONE!");
  }
}

}  // namespace game
