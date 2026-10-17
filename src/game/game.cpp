#include "game/game.h"

#include "game/render.h"

namespace game {

bool Game::Init(lib::Arena& arena, const char* level_text, size_t level_length, const char** error) {
  if (!ParseLevel(level_text, level_length, arena, &level_, error)) {
    return false;
  }
  player_.Spawn(level_.start_x, level_.start_y);
  camera_.CenterOn(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2,
                   level_.pixel_width(), level_.pixel_height());
  return true;
}

void Game::Update(const platform::InputState& input) {
  UpdatePlayer(player_, input, level_);
  camera_.Follow(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2, level_.pixel_width(),
                 level_.pixel_height());
}

void Game::Render(gfx::Surface& screen) const { RenderWorld(screen, level_, camera_, player_); }

}  // namespace game
