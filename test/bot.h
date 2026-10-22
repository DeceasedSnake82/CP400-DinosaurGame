// Look-ahead bot used by the tests: plays the game through the same controls a human has.
#pragma once
#include "../game.h"

using namespace dino;

static const Input NONE_IN = {false, false, false};

// Fairness: a bot that searches ahead by simulating copies of the game, the way a player
// reads the screen: it plans a sequence of manoeuvres (jump with a chosen hold time, duck,
// fast fall) that clears every obstacle that is *currently visible*, then re-plans when new
// ones appear. Obstacles spawned inside a simulation are discarded, so only what the player
// can see is planned. If such a sequence always exists, every pattern the game can spawn is
// passable.
enum Plan { P_SHORT, P_JUMP, P_DUCK, P_FALL, P_COUNT };

static Input planInput(Plan p, int t, bool jumping) {
    Input in = {false, false, false};
    switch (p) {
        case P_SHORT: in.jumpPress = t == 0; in.jumpHeld = t < 10; break;
        case P_JUMP:  in.jumpPress = t == 0; in.jumpHeld = t < 45; break;
        case P_DUCK:  in.duckHeld = t < 24; break;
        case P_FALL:  in.jumpPress = t == 0; in.jumpHeld = t < 14; in.duckHeld = t >= 14 && jumping; break;
        default: break;
    }
    return in;
}

struct Sim {
    Game g;
    uint32_t seen;
    explicit Sim(const Game& s) : g(s), seen(s.spawnCount) {}
    bool alive() const { return g.phase == PH_RUNNING; }
    // One tick; obstacles that spawn during the simulation are dropped again.
    bool step(const Input& in) {
        dino::step(g, in);
        const uint32_t added = g.spawnCount - seen;
        g.nObs -= (int)added;
        seen = g.spawnCount;
        return alive();
    }
    bool passed() const {
        if (g.jumping) return false;
        for (int i = 0; i < g.nObs; ++i) if ((int)(g.obs[i].x >> 16) + g.obs[i].w >= DINO_X - 2) return false;
        return true;
    }
};

// Idle until every visible obstacle is behind the dino.
static bool clearsIdle(Sim s) {
    for (int t = 0; t < 400 && !s.passed(); ++t) if (!s.step(NONE_IN)) return false;
    return s.alive();
}

// Wait `delay` ticks, then run a manoeuvre to its natural end.
static bool runPlan(Sim& s, Plan p, int delay) {
    for (int t = 0; t < delay; ++t) if (!s.step(NONE_IN)) return false;
    for (int t = 0; t < 120; ++t) {
        if (!s.step(planInput(p, t, s.g.jumping))) return false;
        if (p == P_DUCK ? t >= 23 : (t > 2 && !s.g.jumping)) break;
    }
    return true;
}

static bool search(const Sim& s, int depth, Plan* outPlan, int* outDelay) {
    if (clearsIdle(s)) { if (outPlan) *outPlan = P_COUNT; return true; }
    if (depth == 0) return false;
    for (int delay = 0; delay <= 64; ++delay) {
        for (int p = 0; p < P_COUNT; ++p) {
            Sim n = s;
            if (!runPlan(n, (Plan)p, delay)) continue;
            if (search(n, depth - 1, nullptr, nullptr)) {
                if (outPlan) { *outPlan = (Plan)p; *outDelay = delay; }
                return true;
            }
        }
    }
    return false;
}

struct Bot {
    Plan plan = P_COUNT;
    int delay = 0, t = 0;
    bool committed = false;
    long searches = 0, doomed = 0;

    Input act(const Game& g) {
        if (!committed) {
            Sim s(g);
            if (clearsIdle(s)) return NONE_IN;
            ++searches;
            plan = P_COUNT;
            for (int depth = 1; depth <= 4 && plan == P_COUNT; ++depth) {
                Plan p = P_COUNT; int d = 0;
                if (search(s, depth, &p, &d) && p != P_COUNT) { plan = p; delay = d; }
            }
            if (plan == P_COUNT) { plan = P_JUMP; delay = 0; ++doomed; }   // nothing works: jump and hope
            committed = true; t = 0;
        }
        Input in = t < delay ? NONE_IN : planInput(plan, t - delay, g.jumping);
        ++t;
        const int rel = t - delay;
        if (rel > 2 && (plan == P_DUCK ? rel > 24 : !g.jumping)) committed = false;
        return in;
    }
};

