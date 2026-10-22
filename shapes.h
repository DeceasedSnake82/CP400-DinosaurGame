// Sprites converted from the ASCII art in art.h into run-length rows (for fast
// drawing) plus per-row left/right extents (for collision tests). Built once by
// init() into static pools; no heap, no SDK dependencies.
#pragma once

#include <stdint.h>

namespace shapes {

enum Id : uint8_t {
    DINO_IDLE, DINO_RUN_A, DINO_RUN_B, DINO_BLINK, DINO_DEAD, DINO_DUCK_A, DINO_DUCK_B,
    CACTUS_SMALL_A, CACTUS_SMALL_B, CACTUS_LARGE_A, CACTUS_LARGE_B,
    PTERA_UP, PTERA_DOWN,
    CLOUD, STAR_A, STAR_B, RESTART, BUMP_A, BUMP_B,
    MOON_0, MOON_1, MOON_2, MOON_3, MOON_4, MOON_5, MOON_6,   // generated, MOON_3 is the full moon
    COUNT
};

constexpr int NUM_MOON_PHASES = 7;

struct Sprite {
    uint8_t w, h;
    const uint16_t* rowStart;   // h + 1 entries, index into runs (in pairs)
    const uint8_t* runs;        // (x, len) pairs
    const uint8_t* spanL;       // leftmost ink x per row (0xFF = empty row)
    const uint8_t* spanR;       // rightmost ink x per row
};

void init();                    // idempotent
const Sprite& get(Id id);

// Do the silhouettes of a (at ax,ay) and b (at bx,by) overlap? `shrink` pixels are
// trimmed from both sides of every row of both sprites (forgiving, like Chrome's
// inset hit boxes).
bool overlap(const Sprite& a, int ax, int ay, const Sprite& b, int bx, int by, int shrink);

}  // namespace shapes
