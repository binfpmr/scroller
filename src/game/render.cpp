#include "game/render.h"

#include "gfx/draw.h"

namespace game {
namespace {

using gfx::Color;
using gfx::Rgb;

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

// 16x16 brick tile; a grass top when the tile above is open.
Color TilePixel(int x, int y, bool grass) {
  if (grass && y < 4) {
    return y == 3 ? Rgb(40, 110, 30) : Rgb(80, 180, 60);
  }
  const bool mortar = (y % 8) == 7 || ((x + ((y / 8) % 2) * 8) % 16) == 15;
  if (mortar) {
    return Rgb(90, 60, 50);
  }
  return (x + y) % 5 == 0 ? Rgb(170, 90, 60) : Rgb(185, 100, 65);
}

void DrawTiles(gfx::Surface& screen, const Level& level, const Camera& camera) {
  const int first_tx = camera.x >> kTileShift;
  const int first_ty = camera.y >> kTileShift;
  const int last_tx = (camera.x + screen.width - 1) >> kTileShift;
  const int last_ty = (camera.y + screen.height - 1) >> kTileShift;
  for (int ty = first_ty; ty <= last_ty; ++ty) {
    for (int tx = first_tx; tx <= last_tx; ++tx) {
      if (!level.IsSolid(tx, ty)) {
        continue;
      }
      const bool grass = !level.IsSolid(tx, ty - 1);
      const int sx = tx * kTileSize - camera.x;
      const int sy = ty * kTileSize - camera.y;
      const gfx::Rect r = gfx::ClipRect(screen, sx, sy, kTileSize, kTileSize);
      for (int y = r.y; y < r.y + r.height; ++y) {
        Color* row = screen.Row(y);
        for (int x = r.x; x < r.x + r.width; ++x) {
          row[x] = TilePixel(x - sx, y - sy, grass);
        }
      }
    }
  }
}

void DrawPlayer(gfx::Surface& screen, const Player& player, const Camera& camera) {
  const int sx = player.pixel_x() - camera.x;
  const int sy = player.pixel_y() - camera.y;
  gfx::FillRect(screen, sx, sy, Player::kWidth, Player::kHeight, Rgb(40, 30, 60));
  gfx::FillRect(screen, sx + 1, sy + 1, Player::kWidth - 2, Player::kHeight - 2, Rgb(240, 150, 40));
  const int eye_x = player.facing_left ? sx + 2 : sx + Player::kWidth - 5;
  gfx::FillRect(screen, eye_x, sy + 3, 3, 3, Rgb(255, 255, 255));
  gfx::FillRect(screen, player.facing_left ? eye_x : eye_x + 1, sy + 4, 2, 2, Rgb(20, 20, 20));
}

}  // namespace

void RenderWorld(gfx::Surface& screen, const Level& level, const Camera& camera, const Player& player) {
  DrawSky(screen, camera, level);
  DrawTiles(screen, level, camera);
  DrawPlayer(screen, player, camera);
}

}  // namespace game
