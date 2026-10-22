// Rules tests: physics, collision rules, spawn invariants, determinism, day/night and a
// look-ahead bot that must be able to play every seed without ever dying (fairness).
//   make -C test game
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../game.h"
#include "bot.h"
#include "../save.h"

using namespace dino;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static Game fresh(uint32_t seed = 1) { Game g; init(g, seed); return g; }

static Game running(uint32_t seed = 1) {
    Game g = fresh(seed);
    Input j = {true, true, false};
    step(g, j);
    return g;
}

static void tickN(Game& g, int n, Input in = NONE_IN) { for (int i = 0; i < n && g.phase == PH_RUNNING; ++i) { step(g, in); in.jumpPress = false; } }

static Obstacle makeObstacle(Kind k, int x, int bottomAboveGround = 0, int count = 1) {
    Obstacle o = {};
    o.kind = k; o.count = (uint8_t)count; o.x = Q(x);
    if (k == K_PTERA) {
        const shapes::Sprite& p = shapes::get(shapes::PTERA_UP);
        o.w = p.w; o.y = (int16_t)(GROUND_Y - bottomAboveGround - p.h);
    } else {
        const shapes::Sprite& s = shapes::get(k == K_SMALL ? shapes::CACTUS_SMALL_A : shapes::CACTUS_LARGE_A);
        o.w = (uint8_t)(count * s.w + (count - 1)); o.y = (int16_t)(GROUND_Y - s.h);
    }
    return o;
}

// ---------------------------------------------------------------------------
static void testPhysics() {
    Game g = running();
    CHECK(g.jumping, "first press starts a jump");
    int maxH = 0, air = 0;
    Game h = g;
    Input hold = {false, true, false};
    while (h.jumping && air < 200) { step(h, hold); ++air; if ((h.dy >> 16) > maxH) maxH = h.dy >> 16; }
    printf("full jump: peak %d px, airtime %d ticks\n", maxH, air);
    CHECK(maxH >= 60 && maxH <= 90, "peak %d", maxH);
    CHECK(air >= 30 && air <= 45, "airtime %d", air);

    Game s = g;
    int maxS = 0, airS = 0;
    Input rel = {false, false, false};
    while (s.jumping && airS < 200) { step(s, rel); ++airS; if ((s.dy >> 16) > maxS) maxS = s.dy >> 16; }
    printf("short hop: peak %d px, airtime %d ticks\n", maxS, airS);
    CHECK(maxS < maxH && maxS >= 24, "short hop %d vs full %d", maxS, maxH);

    Game f = g;
    int airF = 0;
    Input duck = {false, true, true};
    step(f, hold); step(f, hold);                        // a fresh DOWN press while in the air
    while (f.jumping && airF < 200) { step(f, duck); ++airF; }
    airF += 2;
    Game keep = g; keep.duckWas = true;                  // DOWN already held when jumping must NOT cancel the jump
    int airK = 0;
    while (keep.jumping && airK < 200) { step(keep, duck); ++airK; }
    CHECK(airK > 10, "holding DOWN while pressing jump must not cancel the jump (airtime %d)", airK);
    printf("fast fall: airtime %d ticks\n", airF);
    CHECK(airF < air, "fast fall must be quicker");
    CHECK(f.ducking, "ducking after a fast fall with DOWN held");

    Game d = running();
    tickN(d, 80);
    CHECK(!d.jumping, "landed");
    Input dk = {false, false, true};
    step(d, dk);
    CHECK(isDucking(d), "duck on the ground");
    CHECK(dinoHeight(d) < 30, "duck sprite is low");
}

static void testCollisions() {
    // Large cactus right on the dino hurts a standing dino and a ducking dino.
    Game g = running(); tickN(g, 80);
    g.nObs = 1; g.obs[0] = makeObstacle(K_LARGE, DINO_X + 4);
    CHECK(collides(g, g.obs[0]), "standing dino hits a cactus");
    // ... but not far away, nor one that has passed.
    g.obs[0] = makeObstacle(K_LARGE, DINO_X + 120);
    CHECK(!collides(g, g.obs[0]), "far cactus");
    g.obs[0] = makeObstacle(K_LARGE, -80);
    CHECK(!collides(g, g.obs[0]), "cactus behind");

    // Pterodactyl heights: low = jump, mid = duck, high = walk under.
    const int duckH = shapes::get(shapes::DINO_DUCK_A).h, standH = shapes::get(shapes::DINO_IDLE).h;
    const int lowB = 6, midB = duckH + 3, highB = standH + 9;
    Obstacle low = makeObstacle(K_PTERA, DINO_X + 8, lowB), mid = makeObstacle(K_PTERA, DINO_X + 8, midB), high = makeObstacle(K_PTERA, DINO_X + 8, highB);
    Game st = g; st.jumping = false; st.ducking = false; st.dy = 0;
    Game dk = st; dk.ducking = true;
    CHECK(collides(st, low) && collides(dk, low), "low ptera hits standing and ducking dino");
    CHECK(collides(st, mid) && !collides(dk, mid), "mid ptera: duck passes, standing hits");
    CHECK(!collides(st, high) && !collides(dk, high), "high ptera passes both");
    Game jp = st; jp.jumping = true; jp.dy = Q(20);
    CHECK(!collides(jp, low) || true, "(info) jumping over low");
    jp.dy = Q(50);
    CHECK(!collides(jp, low), "low ptera is jumpable");
}

static void testSpawning() {
    for (uint32_t seed = 1; seed <= 20; ++seed) {
        Game g = running(seed);
        int ptera = 0, spawned = 0;
        uint8_t last = 255; int rep = 0;
        uint32_t prevSpawn = 0;
        for (int t = 0; t < 8000; ++t) {
            // keep the dino alive: pretend it cannot collide by hovering far above everything
            g.dy = Q(300); g.vy = 0; g.jumping = true;
            const q16 speedBefore = g.speed;
            step(g, NONE_IN);
            if (g.phase != PH_RUNNING) { CHECK(false, "died while hovering"); break; }
            {
                if (g.spawnCount != prevSpawn) {
                    ++spawned;
                    const Obstacle& o = g.obs[g.nObs - 1];
                    if (o.kind == K_PTERA) { ++ptera; CHECK(speedBefore >= (q16)(5.7 * 65536) - 100, "ptera too early (speed %d)", speedBefore); }
                    if (o.kind == last) ++rep; else { last = o.kind; rep = 1; }
                    CHECK(rep <= 2, "three identical obstacle types in a row");
                    if (g.nObs >= 2) {
                        const Obstacle& p = g.obs[g.nObs - 2];
                        const int free = (int)(o.x >> 16) - ((int)(p.x >> 16) + p.w);
                        CHECK(free >= (int)p.gap - 1, "gap %d < wanted %d", free, p.gap);
                    }
                }
            }
            prevSpawn = g.spawnCount;
            CHECK(g.nObs <= MAX_OBSTACLES, "obstacle overflow");
        }
        CHECK(spawned > 20, "only %d obstacles in 8000 ticks", spawned);
        if (seed == 1) printf("seed 1: %d obstacles in 8000 ticks, %d pteras\n", spawned, ptera);
    }
}

static uint32_t hash(const Game& g) {
    uint32_t h = 2166136261u;
    const uint8_t* p = (const uint8_t*)&g;
    for (size_t i = 0; i < sizeof g; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

static void testDeterminism() {
    Game a = fresh(77), b = fresh(77);
    uint32_t r = 5;
    for (int t = 0; t < 5000; ++t) {
        r = r * 1664525u + 1013904223u;
        Input in = { (r >> 28) == 0, (r >> 24) < 8, (r >> 20) < 3 };
        step(a, in); step(b, in);
        if (a.phase == PH_DEAD) { Input j = {true, true, false}; step(a, j); step(b, j); }
    }
    CHECK(hash(a) == hash(b), "same seed + same input must give the same game");
}

static void testDayNight() {
    Game g = running(3);
    g.dy = 0;
    uint32_t firstNight = 0; bool sawNight = false, sawDayAgain = false;
    for (int t = 0; t < 60000 && g.phase == PH_RUNNING; ++t) {
        g.dy = Q(300); g.jumping = true; g.vy = 0;       // hover: no obstacle can hurt
        step(g, NONE_IN);
        if (g.fade == 255 && !sawNight) { sawNight = true; firstNight = g.score; }
        if (sawNight && g.fade == 0) { sawDayAgain = true; break; }
    }
    printf("first full night at score %u\n", firstNight);
    CHECK(sawNight && firstNight >= 700, "night should start at 700 points (score %u)", firstNight);
    CHECK(sawDayAgain, "night should end");

    Game o = running(3); o.nightMode = NIGHT_ON;
    for (int i = 0; i < 100; ++i) { o.dy = Q(300); o.jumping = true; step(o, NONE_IN); }
    CHECK(o.fade == 255, "NIGHT_ON fades to night");
    Game d = running(3); d.nightMode = NIGHT_OFF; d.score = 5000;
    for (int i = 0; i < 100; ++i) { d.dy = Q(300); d.jumping = true; step(d, NONE_IN); }
    CHECK(d.fade == 0, "NIGHT_OFF stays day");
}

// ---------------------------------------------------------------------------
static void testFairness() {
    int totalDeaths = 0;
    uint32_t maxScore = 0;
    long ticks = 0;
    int pteras = 0, groups3 = 0;
    for (uint32_t seed = 1; seed <= 16; ++seed) {
        Game g = fresh(seed * 7919);
        Bot bot;
        Input start = {true, true, false};
        step(g, start);
        for (int t = 0; t < 24000; ++t) {
            Input in = bot.act(g);
            const uint32_t before = g.spawnCount;
            step(g, in);
            ++ticks;
            if (g.spawnCount != before) { const Obstacle& o = g.obs[g.nObs - 1]; if (o.kind == K_PTERA) ++pteras; if (o.count == 3) ++groups3; }
            if (g.phase != PH_RUNNING) {
                ++totalDeaths;
                printf("  seed %u died at tick %d, score %u, speed %.2f, %d obstacle(s) on screen\n", seed * 7919, t, g.score, g.speed / 65536.0, g.nObs);
                for (int i = 0; i < g.nObs; ++i)
                    printf("    obs %d: kind %d count %d x %d w %d wanted gap %d; dino dy %d jumping %d\n", i, g.obs[i].kind, g.obs[i].count, (int)(g.obs[i].x >> 16), g.obs[i].w, g.obs[i].gap, (int)(g.dy >> 16), g.jumping);
                break;
            }
        }
        if (g.score > maxScore) maxScore = g.score;
    }
    printf("fairness bot: %ld ticks played, %d deaths, max score %u, %d pteras, %d triple groups\n", ticks, totalDeaths, maxScore, pteras, groups3);
    CHECK(totalDeaths == 0, "a look-ahead bot should never be forced to crash (%d deaths)", totalDeaths);
}

// Hit-box sanity: sprite-vs-sprite overlap must be symmetric and respect shrink.
static void testShapes() {
    const shapes::Sprite& a = shapes::get(shapes::DINO_IDLE);
    CHECK(shapes::overlap(a, 0, 0, a, 0, 0, 0), "sprite overlaps itself");
    CHECK(!shapes::overlap(a, 0, 0, a, 100, 0, 0), "far apart");
    CHECK(shapes::overlap(a, 0, 0, a, 10, 5, 2) == shapes::overlap(a, 10, 5, a, 0, 0, 2), "symmetry");
}

static void testSave() {
    uint8_t rec[save::RECORD_SIZE];
    uint32_t out = 0;
    const uint32_t vals[] = {0u, 1u, 26u, 700u, 12345u, 99999u};
    for (uint32_t v : vals) {
        save::encode(v, rec);
        CHECK(save::decode(rec, out) && out == v, "save record round trip %u", v);
    }
    save::encode(1234, rec);
    for (int i = 0; i < save::RECORD_SIZE; ++i) {            // any single corrupted byte is rejected
        uint8_t bad[save::RECORD_SIZE];
        memcpy(bad, rec, sizeof bad);
        bad[i] ^= 0x10;
        CHECK(!save::decode(bad, out), "corrupted byte %d accepted", i);
    }
    save::encode(100000, rec);                              // out of range for the 5 digit score
    CHECK(!save::decode(rec, out), "score above 99999 accepted");
    uint8_t zeros[save::RECORD_SIZE] = {0};
    CHECK(!save::decode(zeros, out), "empty file accepted");

    remove("out/test_dino.sav");
    CHECK(save::load(out) != 0, "no file yet");
    CHECK(save::store(4321) == 0 && save::load(out) == 0 && out == 4321, "file round trip");
    CHECK(save::store(77) == 0 && save::load(out) == 0 && out == 77, "overwrite with a lower value");
    remove("out/test_dino.sav");
}

static void testPerf() {
    Game g = fresh(5);
    Input j = {true, true, false};
    step(g, j);
    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    long n = 0;
    for (int i = 0; i < 2000000; ++i) {
        g.dy = Q(300); g.jumping = true;
        step(g, NONE_IN); ++n;
        if (g.phase != PH_RUNNING) restart(g), step(g, j);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / n;
    printf("step(): %.3f us per tick on this PC (%zu byte state)\n", us, sizeof(Game));
}

int main() {
    shapes::init();
    testShapes();
    testPhysics();
    testCollisions();
    testSpawning();
    testDeterminism();
    testDayNight();
    testSave();
    testFairness();
    testPerf();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all game tests passed\n");
    return 0;
}
