// Draws the world: sky gradient, tiles and the animated player.
#pragma once

#include <stdint.h>

#include "game/camera.h"
#include "game/game.h"
#include "game/level.h"
#include "game/player.h"
#include "gfx/surface.h"

namespace game {

void RenderWorld(gfx::Surface& screen, const Level& level, const Camera& camera, const Player& player,
                 const Frames& frames, uint32_t ticks);

// Text centred on the screen on a dark box.
void DrawBanner(gfx::Surface& screen, const char* text);

// The player frame for the current state and time (exposed for tests).
const gfx::Sprite* PlayerFrame(const Player& player, const Frames& frames, uint32_t ticks);

}  // namespace game
