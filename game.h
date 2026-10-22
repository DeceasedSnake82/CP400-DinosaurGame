// Game rules for the Dino game: physics, obstacles, scoring, day/night. Pure logic -
// no drawing and no SDK calls - so it can be tested on a PC. The CPU has no FPU, so
// everything is integer / Q16 fixed point (value * 65536).
//
// One call to step() is one tick. There is no timer on the calculator, so a tick is
// the unit of time; the app decides how often it calls step() (see pace in main.cpp).
// Tunables are in game.cpp (namespace cfg), tuned for ~60 ticks per second.
#pragma once

#include <stdint.h>
#include "shapes.h"

namespace dino {

constexpr int VIEW_W   = 320;
constexpr int VIEW_H   = 208;               // height of the playfield strip
constexpr int GROUND_Y = 170;               // row of the ground line; feet stand on GROUND_Y - 1
constexpr int DINO_X   = 26;

constexpr int MAX_OBSTACLES = 4;
constexpr int MAX_CLOUDS    = 4;
constexpr int NUM_STARS     = 4;

typedef int32_t q16;                        // fixed point, 16 fractional bits
constexpr q16 Q(int px) { return (q16)px << 16; }

enum Phase : uint8_t { PH_IDLE, PH_RUNNING, PH_DEAD };
enum NightMode : uint8_t { NIGHT_AUTO, NIGHT_OFF, NIGHT_ON };
enum Kind : uint8_t { K_SMALL, K_LARGE, K_PTERA };

struct Input {
    bool jumpPress;                         // jump key went down since the last tick
    bool jumpHeld;                          // jump key is down now (variable jump height)
    bool duckHeld;
};

struct Obstacle {
    q16 x;                                  // left edge
    int16_t y;                              // top edge (pixels)
    Kind kind;
    uint8_t count;                          // cactus group size 1..3
    uint8_t variants;                       // bit i = 1: member i uses sprite B
    uint8_t frame, frameTimer;              // ptera wing animation
    q16 dx;                                 // extra speed (ptera flies a little faster / slower)
    uint16_t gap;                           // free distance wanted behind this obstacle
    uint8_t w;                              // pixel width of the whole group
};

struct Cloud { q16 x; int16_t y; };
struct Star  { q16 x; int16_t y; uint8_t phase; };

struct Game {
    // --- state machine
    Phase phase;
    uint32_t rng;
    uint32_t ticks;                         // ticks since the run started
    uint32_t deadTicks;                     // ticks since the crash (restart lockout)
    uint32_t idleTicks;                     // blink animation on the title screen

    // --- world
    q16 speed;
    q16 groundScroll;                       // wraps at GROUND_PERIOD
    Obstacle obs[MAX_OBSTACLES];
    int nObs;
    uint32_t spawnCount;                    // obstacles spawned this run (tests / stats)
    uint8_t lastKind, repeatKind;           // avoid more than two identical obstacles in a row
    Cloud clouds[MAX_CLOUDS];
    int nClouds;

    // --- dino
    q16 dy, vy;                             // height above the ground, vertical speed (up is +)
    bool jumping, ducking, speedDrop, reachedMin;
    bool duckWas;                           // duck key state last tick (fast fall needs a fresh press)
    uint8_t animFrame, animTimer;

    // --- score
    uint32_t score, hiScore;
    uint32_t scoreFrac;                     // Q16 fraction of the score
    uint16_t flashTicks;                    // >0: score blinks (every 100 points)
    uint32_t flashScore;                    // value shown while blinking
    bool newHigh;

    // --- day / night
    NightMode nightMode;
    uint32_t nightIndex;                    // last 700-point block that triggered a night
    uint16_t nightTimer;                    // ticks of night left (auto mode)
    uint8_t fade;                           // 0 = day .. 255 = night
    uint8_t moonPhase;
    q16 moonX;
    Star stars[NUM_STARS];
};

void init(Game& g, uint32_t seed);          // power-on (keeps nothing)
void restart(Game& g);                      // new run (keeps hi score, night mode)
void step(Game& g, const Input& in);

// Geometry helpers shared with the renderer.
shapes::Id dinoSprite(const Game& g);       // which dino frame to draw
int dinoWidth(const Game& g);
int dinoHeight(const Game& g);
bool isDucking(const Game& g);              // ducking sprite in use
int dinoTop(const Game& g);                 // y of the dino sprite's top row
bool collides(const Game& g, const Obstacle& o);

}  // namespace dino
