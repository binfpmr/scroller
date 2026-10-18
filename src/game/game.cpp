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

bool Game::Init(lib::Arena& arena, const LevelText* levels, int level_count, const GameAssets& assets,
                const char** error) {
  static char message[96];
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
      FindFrame(t, tn, "stone", &frames_.stone, message, ms) && FindFrame(t, tn, "flag", &frames_.flag, message, ms) &&
      FindFrame(t, tn, "checkpoint", &frames_.checkpoint, message, ms) &&
      FindFrame(t, tn, "checkpoint_on", &frames_.checkpoint_on, message, ms) &&
      FindFrame(t, tn, "cloud", &frames_.cloud, message, ms) &&
      FindFrame(t, tn, "cloud_big", &frames_.cloud_big, message, ms) &&
      FindFrame(t, tn, "bush", &frames_.bush, message, ms);
  const gfx::SpriteSheet* e = assets.enemies;
  const char* en = "sprites/enemies.txt";
  const bool enemy_frames_ok = frames_ok && FindFrame(e, en, "walker1", &frames_.walker[0], message, ms) &&
                               FindFrame(e, en, "walker2", &frames_.walker[1], message, ms) &&
                               FindFrame(e, en, "jumper", &frames_.jumper, message, ms) &&
                               FindFrame(e, en, "jumper_air", &frames_.jumper_air, message, ms) &&
                               FindFrame(e, en, "flyer1", &frames_.flyer[0], message, ms) &&
                               FindFrame(e, en, "flyer2", &frames_.flyer[1], message, ms);
  if (!enemy_frames_ok) {
    *error = message;
    return false;
  }
  if (level_count < 1 || level_count > kMaxLevels) {
    *error = "need 1 to 8 levels";
    return false;
  }
  for (int i = 0; i < level_count; ++i) {
    const char* level_error = nullptr;
    if (!ParseLevel(levels[i].data, levels[i].size, arena, &levels_[i], &level_error)) {
      lib::Format(message, sizeof(message), "level %d: %s", i + 1, level_error);
      *error = message;
      return false;
    }
  }
  level_count_ = level_count;
  current_ = 0;
  Restart();
  return true;
}

void Game::Restart() {
  respawn_tx_ = level().start_x;
  respawn_ty_ = level().start_y;
  checkpoint_tx_ = checkpoint_ty_ = -1;
  enemy_count_ = level().enemy_count;
  for (int i = 0; i < enemy_count_; ++i) {
    const EnemySpawn& s = level().enemies[i];
    enemies_[i].Spawn(s.kind, s.tx, s.ty);
  }
  Respawn();
  win_ticks_ = 0;
  title_ticks_ = level_count_ > 1 ? kTitleTicks : 0;
}

void Game::Respawn() {
  for (int i = 0; i < enemy_count_; ++i) {
    enemies_[i].Reset();  // so none waits at the checkpoint
  }
  player_ = Player();
  player_.Spawn(respawn_tx_, respawn_ty_);
  camera_.CenterOn(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2,
                   level().pixel_width(), level().pixel_height());
}

bool Game::TouchedTile(Tile tile, int* out_tx, int* out_ty) const {
  const int left = player_.pixel_x() >> kTileShift;
  const int right = (player_.pixel_x() + Player::kWidth - 1) >> kTileShift;
  const int top = player_.pixel_y() >> kTileShift;
  const int bottom = (player_.pixel_y() + Player::kHeight - 1) >> kTileShift;
  for (int ty = top; ty <= bottom; ++ty) {
    for (int tx = left; tx <= right; ++tx) {
      if (level().At(tx, ty) == tile) {
        *out_tx = tx;
        *out_ty = ty;
        return true;
      }
    }
  }
  return false;
}

void Game::Update(const platform::InputState& input) {
  ++ticks_;
  if (win_ticks_ > 0) {
    UpdatePlayer(player_, {}, level());  // the player comes to rest; input is ignored
    if (--win_ticks_ == 0) {
      current_ = (current_ + 1) % level_count_;
      Restart();
    }
    return;
  }
  if (title_ticks_ > 0) {
    --title_ticks_;
  }
  UpdatePlayer(player_, input, level());
  events_ |= player_.events;
  camera_.Follow(player_.pixel_x() + Player::kWidth / 2, player_.pixel_y() + Player::kHeight / 2, level().pixel_width(),
                 level().pixel_height());
  for (int i = 0; i < enemy_count_; ++i) {
    UpdateEnemy(enemies_[i], level());
  }
  for (int i = 0; i < enemy_count_; ++i) {
    if (EnemyTouches(enemies_[i], player_.pixel_x(), player_.pixel_y(), Player::kWidth, Player::kHeight)) {
      Respawn();
      events_ |= kEventHit;
      return;
    }
  }
  int tx = 0, ty = 0;
  if (TouchedTile(Tile::kCheckpoint, &tx, &ty) && (tx != checkpoint_tx_ || ty != checkpoint_ty_)) {
    checkpoint_tx_ = respawn_tx_ = tx;
    checkpoint_ty_ = respawn_ty_ = ty;
    events_ |= kEventCheckpoint;
  }
  if (player_.pixel_y() > level().pixel_height() + kFallMargin) {
    Respawn();
    events_ |= kEventFall;
  }
  if (TouchedTile(Tile::kGoal, &tx, &ty)) {
    win_ticks_ = kWinTicks;
    events_ |= kEventGoal;
  }
}

void Game::Render(gfx::Surface& screen) const {
  RenderWorld(screen, level(), camera_, player_, frames_, ticks_, checkpoint_tx_, checkpoint_ty_, enemies_,
              enemy_count_);
  if (won()) {
    DrawBanner(screen, current_ + 1 == level_count_ && level_count_ > 1 ? "ALL DONE!" : "WELL DONE!");
  } else if (title_ticks_ > 0) {
    char title[16];
    lib::Format(title, sizeof(title), "LEVEL %d", current_ + 1);
    DrawBanner(screen, title);
  }
}

}  // namespace game
