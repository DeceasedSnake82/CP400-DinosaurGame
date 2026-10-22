#include "game.h"

namespace dino {

namespace cfg {
// Speeds are pixels per tick, Q16. Tuned so a 320 px wide field plays like Chrome's 600 px one.
constexpr q16 START_SPEED = 4 << 16;
constexpr q16 MAX_SPEED   = 9 << 16;
constexpr q16 ACCEL       = 46;                 // ~0.0007 px/tick per tick
constexpr q16 GRAVITY     = 39322;              // 0.6 px/tick^2
constexpr q16 JUMP_V0     = 602931;             // 9.2 px/tick (+ a little more at speed)
constexpr q16 JUMP_V_PER_SPEED = 4915;          // 0.075
constexpr q16 DROP_V      = 327680;             // 5.0: early key release caps the climb speed
constexpr q16 FAST_FALL   = 1 << 16;            // initial downward speed of a speed drop
constexpr int FAST_FALL_MUL = 3;
constexpr q16 MIN_JUMP_H  = 24 << 16;           // key release only shortens a jump past this height
constexpr int SCORE_PER_PX = 2458;              // Q16, 0.0375 points per pixel travelled
constexpr uint32_t MAX_SCORE = 99999;

constexpr int ANIM_TICKS  = 5;                  // run-cycle frame length
constexpr int PTERA_TICKS = 9;                  // wing flap
constexpr int CLEAR_TICKS = 110;                // no obstacles for the first moments of a run
constexpr int GAP_TICKS   = 36;                 // min gap = speed * this (+ group width / 2)
constexpr int GAP_RANDOM_PCT = 50;              // gap is min .. min * 1.5
constexpr q16 PTERA_MIN_SPEED = (q16)(5.7 * 65536);
constexpr q16 SMALL2_SPEED = (q16)(4.6 * 65536), SMALL3_SPEED = (q16)(5.6 * 65536);
constexpr q16 LARGE2_SPEED = (q16)(5.0 * 65536), LARGE3_SPEED = (q16)(6.6 * 65536);
constexpr q16 PTERA_SPEED_OFFSET = (q16)(0.6 * 65536);
constexpr int PTERA_LOW_BOTTOM = 6;             // pixels above the ground
constexpr int PTERA_MID_ABOVE_DUCK = 3;         // mid ptera: just clears a ducking dino
constexpr int PTERA_HIGH_ABOVE_STAND = 9;       // high ptera: just clears a standing dino
constexpr int HIT_SHRINK  = 3;                  // collision forgiveness (pixels per side)
constexpr int GROUP_SPACING = 1;                // pixels between cacti of a group

constexpr int CLOUD_Y_MIN = 12, CLOUD_Y_RANGE = 56;
constexpr int CLOUD_GAP_MIN = 90, CLOUD_GAP_RANGE = 200;
constexpr q16 CLOUD_SPEED = 13107;              // 0.2 of the ground speed

constexpr uint32_t NIGHT_EVERY = 700;           // score between nights (like Chrome)
constexpr uint16_t NIGHT_TICKS = 720;
constexpr int FADE_STEP = 3;                    // 255 / 3 = 85 ticks to fade
constexpr q16 MOON_SPEED = 9000;
constexpr q16 STAR_SPEED = 3000;
constexpr uint16_t FLASH_TICKS = 90;
constexpr int DEAD_LOCKOUT = 30;                // ticks before a crash can be dismissed
}  // namespace cfg

namespace {

uint32_t rnd(Game& g) {
    uint32_t x = g.rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g.rng = x;
    return x;
}
int rndRange(Game& g, int lo, int n) { return lo + (int)((rnd(g) >> 8) % (uint32_t)n); }

q16 mulq(q16 a, q16 b) { return (q16)(((int64_t)a * b) >> 16); }

const shapes::Sprite& spr(shapes::Id id) { return shapes::get(id); }

shapes::Id cactusId(Kind k, bool b) {
    return k == K_SMALL ? (b ? shapes::CACTUS_SMALL_B : shapes::CACTUS_SMALL_A)
                        : (b ? shapes::CACTUS_LARGE_B : shapes::CACTUS_LARGE_A);
}

void startJump(Game& g) {
    g.jumping = true;
    g.ducking = false;
    g.speedDrop = false;
    g.reachedMin = false;
    g.vy = cfg::JUMP_V0 + mulq(g.speed, cfg::JUMP_V_PER_SPEED);
}

void begin(Game& g) {
    g.phase = PH_RUNNING;
    startJump(g);
}

int pickGap(Game& g, int widthPx) {
    const int minGap = (int)(((int64_t)g.speed * cfg::GAP_TICKS) >> 16) + widthPx / 2;
    return minGap + rndRange(g, 0, minGap * cfg::GAP_RANDOM_PCT / 100 + 1);
}

void spawnObstacle(Game& g) {
    int kind;
    for (int tries = 0;; ++tries) {
        kind = rndRange(g, 0, g.speed >= cfg::PTERA_MIN_SPEED ? 3 : 2);
        if (!(kind == g.lastKind && g.repeatKind >= 2) || tries > 8) break;
    }
    if (kind == g.lastKind) ++g.repeatKind; else { g.lastKind = (uint8_t)kind; g.repeatKind = 1; }

    Obstacle o = {};
    o.kind = (Kind)kind;
    o.x = Q(VIEW_W);
    if (o.kind == K_PTERA) {
        const shapes::Sprite& p = spr(shapes::PTERA_UP);
        int bottom;
        switch (rndRange(g, 0, 3)) {
            case 0:  bottom = cfg::PTERA_LOW_BOTTOM; break;
            case 1:  bottom = spr(shapes::DINO_DUCK_A).h + cfg::PTERA_MID_ABOVE_DUCK; break;
            default: bottom = spr(shapes::DINO_IDLE).h + cfg::PTERA_HIGH_ABOVE_STAND; break;
        }
        o.count = 1;
        o.w = p.w;
        o.y = (int16_t)(GROUND_Y - bottom - p.h);
        o.dx = (rnd(g) & 0x100) ? cfg::PTERA_SPEED_OFFSET : -cfg::PTERA_SPEED_OFFSET;
        o.frame = (uint8_t)(rnd(g) & 1);
    } else {
        const bool large = o.kind == K_LARGE;
        int maxCount = 1;
        if (large) maxCount = g.speed >= cfg::LARGE3_SPEED ? 3 : (g.speed >= cfg::LARGE2_SPEED ? 2 : 1);
        else       maxCount = g.speed >= cfg::SMALL3_SPEED ? 3 : (g.speed >= cfg::SMALL2_SPEED ? 2 : 1);
        o.count = (uint8_t)rndRange(g, 1, maxCount);
        o.variants = (uint8_t)(rnd(g) >> 12);
        int w = 0, h = 0;
        for (int i = 0; i < o.count; ++i) {
            const shapes::Sprite& s = spr(cactusId(o.kind, (o.variants >> i) & 1));
            w += s.w + (i ? cfg::GROUP_SPACING : 0);
            if (s.h > h) h = s.h;
        }
        o.w = (uint8_t)w;
        o.y = (int16_t)(GROUND_Y - h);
    }
    o.gap = (uint16_t)pickGap(g, o.w);
    g.obs[g.nObs++] = o;
    ++g.spawnCount;
}

void updateObstacles(Game& g) {
    for (int i = 0; i < g.nObs; ++i) {
        Obstacle& o = g.obs[i];
        o.x -= g.speed + o.dx;
        if (o.kind == K_PTERA && ++o.frameTimer >= cfg::PTERA_TICKS) { o.frameTimer = 0; o.frame ^= 1; }
    }
    if (g.nObs && (g.obs[0].x >> 16) + g.obs[0].w < 0) {
        for (int i = 1; i < g.nObs; ++i) g.obs[i - 1] = g.obs[i];
        --g.nObs;
    }
    if (g.nObs == 0) {
        if (g.ticks > cfg::CLEAR_TICKS) spawnObstacle(g);
    } else if (g.nObs < MAX_OBSTACLES) {
        const Obstacle& last = g.obs[g.nObs - 1];
        if ((last.x >> 16) + last.w + (int)last.gap < VIEW_W && (last.x >> 16) + last.w < VIEW_W) spawnObstacle(g);
    }
}

void updateClouds(Game& g, q16 dx) {
    for (int i = 0; i < g.nClouds; ++i) g.clouds[i].x -= dx;
    const int cw = spr(shapes::CLOUD).w;
    if (g.nClouds && (g.clouds[0].x >> 16) + cw < 0) {
        for (int i = 1; i < g.nClouds; ++i) g.clouds[i - 1] = g.clouds[i];
        --g.nClouds;
    }
    if (g.nClouds < MAX_CLOUDS) {
        const bool room = g.nClouds == 0 ||
            (g.clouds[g.nClouds - 1].x >> 16) + cw + cfg::CLOUD_GAP_MIN + (int)(rnd(g) % cfg::CLOUD_GAP_RANGE) < VIEW_W;
        if (room) {
            Cloud& c = g.clouds[g.nClouds++];
            c.x = Q(VIEW_W);
            c.y = (int16_t)rndRange(g, cfg::CLOUD_Y_MIN, cfg::CLOUD_Y_RANGE);
        }
    }
}

void startNight(Game& g) {
    g.nightTimer = cfg::NIGHT_TICKS;
    g.moonPhase = (uint8_t)((g.moonPhase + 1) % shapes::NUM_MOON_PHASES);
    g.moonX = Q(VIEW_W - 40) + (q16)(rnd(g) % 40) * 65536;
    for (Star& s : g.stars) {
        s.x = (q16)(rnd(g) % VIEW_W) * 65536;
        s.y = (int16_t)(8 + rnd(g) % 90);
        s.phase = (uint8_t)(rnd(g) % 60);
    }
}

void updateNight(Game& g) {
    if (g.nightMode == NIGHT_AUTO) {
        const uint32_t idx = g.score / cfg::NIGHT_EVERY;
        if (idx > g.nightIndex) { g.nightIndex = idx; startNight(g); }
        if (g.nightTimer) --g.nightTimer;
    }
    const bool night = g.nightMode == NIGHT_ON || (g.nightMode == NIGHT_AUTO && g.nightTimer > 0);
    if (night) { g.fade = (uint8_t)(g.fade + cfg::FADE_STEP > 255 ? 255 : g.fade + cfg::FADE_STEP); }
    else       { g.fade = (uint8_t)(g.fade < cfg::FADE_STEP ? 0 : g.fade - cfg::FADE_STEP); }
    if (g.fade) {
        g.moonX -= cfg::MOON_SPEED;
        if ((g.moonX >> 16) < -24) g.moonX = Q(VIEW_W + 8);
        for (Star& s : g.stars) {
            s.x -= cfg::STAR_SPEED;
            if ((s.x >> 16) < -8) s.x = Q(VIEW_W + 4);
            ++s.phase;
        }
    }
}

void updateDino(Game& g, const Input& in) {
    if (in.jumpPress && !g.jumping) startJump(g);

    if (g.jumping) {
        if (!in.jumpHeld && g.reachedMin && g.vy > cfg::DROP_V) g.vy = cfg::DROP_V;
        if (in.duckHeld && !g.duckWas && !g.speedDrop) { g.speedDrop = true; g.vy = -cfg::FAST_FALL; }
        g.dy += g.speedDrop ? g.vy * cfg::FAST_FALL_MUL : g.vy;
        g.vy -= cfg::GRAVITY;
        if (g.dy >= cfg::MIN_JUMP_H) g.reachedMin = true;
        if (g.dy <= 0) {
            g.dy = 0; g.vy = 0;
            g.jumping = g.speedDrop = g.reachedMin = false;
            g.ducking = in.duckHeld;
        }
    } else {
        g.ducking = in.duckHeld;
        if (++g.animTimer >= cfg::ANIM_TICKS) { g.animTimer = 0; g.animFrame ^= 1; }
    }
}

void updateScore(Game& g) {
    g.scoreFrac += (uint32_t)(((int64_t)g.speed * cfg::SCORE_PER_PX) >> 16);
    const uint32_t before = g.score / 100;
    g.score += g.scoreFrac >> 16;
    g.scoreFrac &= 0xFFFF;
    if (g.score > cfg::MAX_SCORE) g.score = cfg::MAX_SCORE;
    if (g.score / 100 > before) { g.flashTicks = cfg::FLASH_TICKS; g.flashScore = g.score / 100 * 100; }
    else if (g.flashTicks) --g.flashTicks;
}

}  // namespace

// ---------------------------------------------------------------------------
shapes::Id dinoSprite(const Game& g) {
    using namespace shapes;
    if (g.phase == PH_DEAD) return DINO_DEAD;
    if (g.phase == PH_IDLE) return ((g.idleTicks / 24) % 7 == 6 && (g.idleTicks % 24) < 6) ? DINO_BLINK : DINO_IDLE;
    if (g.jumping) return DINO_IDLE;
    if (g.ducking) return g.animFrame ? DINO_DUCK_B : DINO_DUCK_A;
    return g.animFrame ? DINO_RUN_B : DINO_RUN_A;
}

int dinoWidth(const Game& g)  { return spr(dinoSprite(g)).w; }
int dinoHeight(const Game& g) { return spr(dinoSprite(g)).h; }
bool isDucking(const Game& g) { return !g.jumping && g.ducking; }
int dinoTop(const Game& g)    { return GROUND_Y - dinoHeight(g) - (int)(g.dy >> 16); }

bool collides(const Game& g, const Obstacle& o) {
    const shapes::Sprite& d = spr(dinoSprite(g));
    const int ox = (int)(o.x >> 16);
    if (ox >= DINO_X + d.w || ox + o.w <= DINO_X) return false;
    const int dy = dinoTop(g);
    if (o.kind == K_PTERA) {
        return shapes::overlap(d, DINO_X, dy, spr(o.frame ? shapes::PTERA_DOWN : shapes::PTERA_UP), ox, o.y, cfg::HIT_SHRINK);
    }
    int x = ox;
    for (int i = 0; i < o.count; ++i) {
        const shapes::Sprite& s = spr(cactusId(o.kind, (o.variants >> i) & 1));
        if (shapes::overlap(d, DINO_X, dy, s, x, GROUND_Y - s.h, cfg::HIT_SHRINK)) return true;
        x += s.w + cfg::GROUP_SPACING;
    }
    return false;
}

void restart(Game& g) {
    const uint32_t hi = g.hiScore, rng = g.rng;
    const NightMode nm = g.nightMode;
    const uint8_t moon = g.moonPhase;
    g = Game{};
    g.hiScore = hi;
    g.rng = rng;
    g.nightMode = nm;
    g.moonPhase = moon;
    g.phase = PH_IDLE;
    g.speed = cfg::START_SPEED;
    g.lastKind = 255;
    if (nm == NIGHT_ON) { g.fade = 255; startNight(g); }
    for (int i = 0; i < 2; ++i) {
        Cloud& c = g.clouds[g.nClouds++];
        c.x = (q16)rndRange(g, 40 + i * 150, 110) * 65536;
        c.y = (int16_t)rndRange(g, cfg::CLOUD_Y_MIN, cfg::CLOUD_Y_RANGE);
    }
}

void init(Game& g, uint32_t seed) {
    shapes::init();
    g = Game{};
    g.rng = seed ? seed : 0x2545F491u;
    g.nightMode = NIGHT_AUTO;
    restart(g);
}

void step(Game& g, const Input& in) {
    switch (g.phase) {
    case PH_IDLE:
        ++g.idleTicks;
        if (in.jumpPress) begin(g);
        return;
    case PH_DEAD:
        ++g.deadTicks;
        if (in.jumpPress && g.deadTicks > (uint32_t)cfg::DEAD_LOCKOUT) { restart(g); begin(g); }
        return;
    case PH_RUNNING:
        break;
    }

    ++g.ticks;
    updateDino(g, in);
    g.duckWas = in.duckHeld;

    g.groundScroll += g.speed;
    while (g.groundScroll >= Q(640)) g.groundScroll -= Q(640);
    updateClouds(g, mulq(g.speed, cfg::CLOUD_SPEED));
    updateObstacles(g);

    bool hit = false;
    for (int i = 0; i < g.nObs && !hit; ++i) hit = collides(g, g.obs[i]);
    if (hit) {
        g.phase = PH_DEAD;
        g.deadTicks = 0;
        if (g.score > g.hiScore) { g.hiScore = g.score; g.newHigh = true; }
        return;
    }

    if (g.speed < cfg::MAX_SPEED) { g.speed += cfg::ACCEL; if (g.speed > cfg::MAX_SPEED) g.speed = cfg::MAX_SPEED; }
    updateScore(g);
    updateNight(g);
}

}  // namespace dino
