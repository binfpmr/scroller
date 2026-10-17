// Draws the world with procedural placeholder graphics (sky gradient, bricks with grass, box player).
#pragma once

#include "game/camera.h"
#include "game/level.h"
#include "game/player.h"
#include "gfx/surface.h"

namespace game {

void RenderWorld(gfx::Surface& screen, const Level& level, const Camera& camera, const Player& player);

}  // namespace game
