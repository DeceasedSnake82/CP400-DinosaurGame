// Draws the playfield (sky, scenery, obstacles, dino, score) into the VRAM strip that
// starts at screen row `oy`. Everything outside the strip is left alone.
#pragma once

#include <stdint.h>
#include "game.h"

namespace render {

struct Palette { uint16_t bg, ink, soft, faint; };

Palette palette(uint8_t fade);                           // 0 = day, 255 = night
void drawField(const dino::Game& g, int oy, bool paused);

}  // namespace render
