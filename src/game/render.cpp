#include "game/render.h"

#include "gfx/draw.h"
#include "gfx/font.h"
#include "gfx/sprite.h"

namespace game {
namespace {

using gfx::Color;
using gfx::Rgb;

constexpr int kRunFrameTicks = 6;
constexpr uint32_t kBlinkPeriodTicks = 200;
constexpr uint32_t kBlinkTicks = 8;

void DrawSky(gfx::Surface& screen, const Camera& camera, const Level& level) {
  // Gradient over the whole level height, so vertical scrolling is visible against the sky too.
  const int h = level.pixel_height() > 0 ? level.pixel_height() : 1;
  for (int y = 0; y < screen.height; ++y) {
    const int t = lib::Clamp((camera.y + y) * 255 / h, 0, 255);
    const Color c =
        Rgb(static_cast<uint8_t>(70 + t / 4), static_cast<uint8_t>(120 + t / 4), static_cast<uint8_t>(220 - t / 5));
    Color* row = screen.Row(y);
    for (int x = 0; x < screen.width; ++x) {
      row[x] = c;
    }
  }
}

// Parallax: a layer moves x_num/x_den as fast as the camera horizontally and y_num/y_den vertically. Its base line
// is `lift` pixels above the level's ground (Level::ground_rows) when the camera is at the bottom of the level. With a
// vertical factor of 1 the layer stays on the ground (bushes); smaller factors make it sink as the camera climbs.
struct Layer {
  int x_num;
  int x_den;
  int y_num;
  int y_den;
  int lift;
};

int LayerOffsetX(const Camera& camera, const Layer& layer) { return camera.x * layer.x_num / layer.x_den; }

int LayerBaseY(const gfx::Surface& screen, const Camera& camera, const Level& level, const Layer& layer) {
  const int below_top = lib::Max(level.pixel_height() - screen.height - camera.y, 0);  // how far the camera climbed
  return screen.height - level.ground_rows * kTileSize - layer.lift + below_top * layer.y_num / layer.y_den;
}

int Wrap(int value, int period) { return ((value % period) + period) % period; }

// A rounded bump (parabola) of height `amplitude` repeating every `period` pixels.
int Bump(int x, int period, int amplitude) {
  const int t = 2 * Wrap(x, period) - period;  // -period..period
  return amplitude * (period * period - t * t) / (period * period);
}

// Pseudo-random but fixed per index, for placing clouds and bushes.
uint32_t Hash(int i) {
  uint32_t h = static_cast<uint32_t>(i) * 2654435761u;
  return h ^ (h >> 15);
}

void DrawHills(gfx::Surface& screen, const Camera& camera, const Level& level, const Layer& layer, int height,
               int period1, int amplitude1, int period2, int amplitude2, Color fill, Color edge) {
  const int base = LayerBaseY(screen, camera, level, layer);
  const int offset = LayerOffsetX(camera, layer);
  for (int x = 0; x < screen.width; ++x) {
    const int lx = x + offset;
    const int top = base - height - Bump(lx, period1, amplitude1) - Bump(lx + period2 / 3, period2, amplitude2);
    for (int y = lib::Max(top, 0); y < screen.height; ++y) {
      screen.Row(y)[x] = y < top + 2 ? edge : fill;
    }
  }
}

// Sprites spaced along a layer; every third one is `big` instead (if given).
void DrawSprites(gfx::Surface& screen, const Camera& camera, const Level& level, const Layer& layer,
                 const gfx::Sprite& sprite, const gfx::Sprite* big, int spacing, int jitter_x, int min_up,
                 int range_up) {
  const int base = LayerBaseY(screen, camera, level, layer);
  const int offset = LayerOffsetX(camera, layer);
  for (int i = offset / spacing - 1; i * spacing - offset < screen.width + spacing; ++i) {
    const uint32_t h = Hash(i);
    const int x = i * spacing + static_cast<int>(h % static_cast<uint32_t>(jitter_x)) - offset;
    const int up = min_up + (range_up > 0 ? static_cast<int>((h >> 8) % static_cast<uint32_t>(range_up)) : 0);
    const gfx::Sprite& chosen = big != nullptr && (h >> 20) % 3 == 0 ? *big : sprite;
    gfx::DrawSprite(screen, chosen, x, base - up - chosen.height);
  }
}

void DrawBackground(gfx::Surface& screen, const Camera& camera, const Level& level, const Frames& frames) {
  DrawSky(screen, camera, level);
  // Clouds high above the hills, some big; far hills without an outline; near hills; bushes on the ground.
  DrawSprites(screen, camera, level, {1, 6, 1, 6, 0}, *frames.cloud, frames.cloud_big, 150, 70, 60, 60);
  const Color far = Rgb(116, 170, 168);
  DrawHills(screen, camera, level, {1, 4, 1, 4, 8}, 30, 230, 34, 120, 14, far, far);
  DrawHills(screen, camera, level, {1, 2, 1, 2, 0}, 10, 170, 28, 90, 10, Rgb(84, 160, 92), Rgb(110, 190, 110));
  DrawSprites(screen, camera, level, {3, 4, 1, 1, 0}, *frames.bush, nullptr, 110, 60, 0, 0);
}

void DrawTiles(gfx::Surface& screen, const Level& level, const Camera& camera, const Frames& frames, int checkpoint_tx,
               int checkpoint_ty) {
  const int first_tx = camera.x >> kTileShift;
  const int first_ty = camera.y >> kTileShift;
  const int last_tx = (camera.x + screen.width - 1) >> kTileShift;
  const int last_ty = (camera.y + screen.height - 1) >> kTileShift;
  for (int ty = first_ty; ty <= last_ty; ++ty) {
    for (int tx = first_tx; tx <= last_tx; ++tx) {
      if (level.IsCave(tx, ty)) {  // under a thick ceiling: dark earth instead of the sky
        gfx::FillRect(screen, tx * kTileSize - camera.x, ty * kTileSize - camera.y, kTileSize, kTileSize,
                      Rgb(52, 40, 36));  // then the tile itself (a flag, a goal) as usual
      }
      const gfx::Sprite* sprite = nullptr;
      switch (level.At(tx, ty)) {
        case Tile::kGround:
          sprite = level.IsSolid(tx, ty - 1) ? frames.ground : frames.ground_top;
          break;
        case Tile::kStone:
          sprite = frames.stone;
          break;
        case Tile::kGoal:
          sprite = frames.flag;
          break;
        case Tile::kCheckpoint:
          sprite = tx == checkpoint_tx && ty == checkpoint_ty ? frames.checkpoint_on : frames.checkpoint;
          break;
        case Tile::kEmpty:
          break;
      }
      if (sprite != nullptr) {
        gfx::DrawSprite(screen, *sprite, tx * kTileSize - camera.x, ty * kTileSize - camera.y);
      }
    }
  }
}

}  // namespace

const gfx::Sprite* PlayerFrame(const Player& player, const Frames& frames, uint32_t ticks) {
  if (player.ducking) {
    return frames.duck;
  }
  if (!player.on_ground) {
    return player.vy < lib::Fixed() ? frames.jump : frames.fall;
  }
  if (player.vx != lib::Fixed()) {
    return frames.run[(ticks / kRunFrameTicks) % 4];
  }
  return ticks % kBlinkPeriodTicks < kBlinkTicks ? frames.blink : frames.idle;
}

void RenderWorld(gfx::Surface& screen, const Level& level, const Camera& camera, const Player& player,
                 const Frames& frames, uint32_t ticks, int checkpoint_tx, int checkpoint_ty, const Enemy* enemies,
                 int enemy_count) {
  DrawBackground(screen, camera, level, frames);
  DrawTiles(screen, level, camera, frames, checkpoint_tx, checkpoint_ty);
  for (int i = 0; i < enemy_count; ++i) {
    const Enemy& e = enemies[i];
    const gfx::Sprite* sprite = nullptr;
    switch (e.kind) {
      case EnemyKind::kWalker:
        sprite = frames.walker[(e.ticks / 12) & 1];
        break;
      case EnemyKind::kJumper:
        sprite = e.on_ground ? frames.jumper : frames.jumper_air;
        break;
      case EnemyKind::kFlyer:
        sprite = frames.flyer[(e.ticks / 6) & 1];
        break;
    }
    // 16x16 sprite centred on the bottom edge of the 12x12 hitbox.
    gfx::DrawSprite(screen, *sprite, e.pixel_x() - 2 - camera.x, e.pixel_y() + Enemy::kHeight - 16 - camera.y,
                    e.dir < 0);
  }
  // The 16x16 frame is centred horizontally on the 12x14 hitbox, feet on its bottom edge.
  const gfx::Sprite& sprite = *PlayerFrame(player, frames, ticks);
  const int x = player.pixel_x() + (Player::kWidth - sprite.width) / 2 - camera.x;
  const int y = player.pixel_y() + Player::kHeight - sprite.height - camera.y;
  gfx::DrawSprite(screen, sprite, x, y, player.facing_left);
}

void DrawBanner(gfx::Surface& screen, const char* text) {
  const int w = gfx::TextWidth(text) + 16;
  const int h = gfx::kGlyphHeight + 12;
  const int x = (screen.width - w) / 2;
  const int y = (screen.height - h) / 2;
  gfx::FillRect(screen, x, y, w, h, Rgb(20, 20, 40));
  gfx::DrawText(screen, x + 8, y + 6, text, Rgb(255, 230, 120));
}

void DrawHelp(gfx::Surface& screen) {
  struct Line {
    const char* keys;
    const char* what;
  };
  static constexpr Line kLines[] = {
      {"LEFT / RIGHT", "RUN"},
      {"UP / SPACE", "JUMP (HOLD: HIGHER)"},
      {"DOWN", "DUCK"},
      {"DOWN, THEN JUMP", "DUCK-JUMP (HIGHER)"},
      {"HOLD DOWN 1 S", "DUCK-JUMP BY ITSELF"},
      {"JUMP AT A WALL", "WALL-JUMP"},
      {"+ / -", "VOLUME"},
      {"M", "MUTE"},
      {"N", "MUSIC ON/OFF"},
      {"F1", "STATISTICS"},
      {"F2", "NEXT LEVEL"},
      {"CTRL+ALT+R", "RELOAD (NETWORK BOOT)"},
      {"CTRL+ALT+DEL", "REBOOT"},
      {"ESC", "THIS HELP ON/OFF"},
  };
  constexpr int kLineCount = sizeof(kLines) / sizeof(kLines[0]);
  constexpr int kKeyColumn = 16 * gfx::kCharAdvance;
  int what_width = 0;
  for (const Line& l : kLines) {
    what_width = lib::Max(what_width, gfx::TextWidth(l.what));
  }
  const int w = kKeyColumn + what_width + 16;
  const int h = (kLineCount + 2) * gfx::kLineHeight + 12;
  const int x = (screen.width - w) / 2;
  const int y = (screen.height - h) / 2;
  gfx::FillRect(screen, x, y, w, h, Rgb(20, 20, 40));
  gfx::DrawText(screen, x + 8, y + 6, "KEYS", Rgb(255, 230, 120));
  for (int i = 0; i < kLineCount; ++i) {
    const int ly = y + 6 + (i + 2) * gfx::kLineHeight;
    gfx::DrawText(screen, x + 8, ly, kLines[i].keys, Rgb(255, 230, 120));
    gfx::DrawText(screen, x + 8 + kKeyColumn, ly, kLines[i].what, Rgb(255, 255, 255));
  }
}

}  // namespace game
