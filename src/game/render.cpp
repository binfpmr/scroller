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

void DrawTiles(gfx::Surface& screen, const Level& level, const Camera& camera, const Frames& frames) {
  const int first_tx = camera.x >> kTileShift;
  const int first_ty = camera.y >> kTileShift;
  const int last_tx = (camera.x + screen.width - 1) >> kTileShift;
  const int last_ty = (camera.y + screen.height - 1) >> kTileShift;
  for (int ty = first_ty; ty <= last_ty; ++ty) {
    for (int tx = first_tx; tx <= last_tx; ++tx) {
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
                 const Frames& frames, uint32_t ticks) {
  DrawSky(screen, camera, level);
  DrawTiles(screen, level, camera, frames);
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

}  // namespace game
