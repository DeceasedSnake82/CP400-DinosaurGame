// Renderer checks + screenshots of bot-played game states.
//   make -C test render        (writes PPMs to out/; `make shots` converts them to PNG)
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../game.h"
#include "../render.h"
#include "../gfx.h"
#include "bot.h"

static uint16_t g_vram[320 * 528];
static uint16_t* host_vram() { return g_vram; }
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

constexpr int FIELD_Y = 40;

static void savePPM(const char* name, int y0, int h) {
    char path[128];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n320 %d\n255\n", h);
    for (int i = y0 * 320; i < (y0 + h) * 320; ++i) {
        const uint16_t c = g_vram[i];
        const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void drawAndCheck(const Game& g, bool paused) {
    const uint16_t sentinel = 0x1234;
    for (int i = 0; i < 320 * 528; ++i) g_vram[i] = sentinel;
    render::drawField(g, FIELD_Y, paused);
    for (int i = 0; i < FIELD_Y * 320; ++i) if (g_vram[i] != sentinel) { CHECK(false, "wrote above the strip (pixel %d)", i); return; }
    for (int i = (FIELD_Y + VIEW_H) * 320; i < 320 * 528; ++i) if (g_vram[i] != sentinel) { CHECK(false, "wrote below the strip (pixel %d)", i); return; }
    for (int i = FIELD_Y * 320; i < (FIELD_Y + VIEW_H) * 320; ++i) if (g_vram[i] == sentinel) { CHECK(false, "strip pixel %d never painted", i); return; }
}

int main() {
    gfx::init();
    Game g;
    init(g, 424242);
    Bot bot;
    Input start = {true, true, false};
    step(g, start);

    // palette endpoints
    const render::Palette day = render::palette(0), night = render::palette(255);
    CHECK(day.bg == gfx::rgb(247, 247, 247) && night.bg == gfx::rgb(10, 10, 12), "palette endpoints");
    CHECK(render::palette(128).bg != day.bg && render::palette(128).bg != night.bg, "mid fade differs");

    bool shotDay = false, shotPtera = false, shotNight = false, shotGroup = false, shotFade = false;
    for (int t = 0; t < 40000 && g.phase == PH_RUNNING; ++t) {
        step(g, bot.act(g));
        if (t % 97 == 0) drawAndCheck(g, false);                    // invariants on lots of states

        bool ptera = false, group = false;
        for (int i = 0; i < g.nObs; ++i) { if (g.obs[i].kind == K_PTERA && (g.obs[i].x >> 16) < 200 && (g.obs[i].x >> 16) > 40) ptera = true; if (g.obs[i].count == 3 && (g.obs[i].x >> 16) < 220) group = true; }
        if (!shotDay && t == 300 && g.fade == 0 && g.nObs) { drawAndCheck(g, false); savePPM("r_day", FIELD_Y, VIEW_H); shotDay = true; }
        if (!shotPtera && ptera && g.fade == 0) { drawAndCheck(g, false); savePPM("r_ptera", FIELD_Y, VIEW_H); shotPtera = true; }
        if (!shotGroup && group && g.fade == 0) { drawAndCheck(g, false); savePPM("r_group", FIELD_Y, VIEW_H); shotGroup = true; }
        if (!shotFade && g.fade > 100 && g.fade < 160 && g.nObs) { drawAndCheck(g, false); savePPM("r_fade", FIELD_Y, VIEW_H); shotFade = true; }
        if (!shotNight && g.fade == 255 && g.nObs) { drawAndCheck(g, false); savePPM("r_night", FIELD_Y, VIEW_H); shotNight = true; }
    }
    CHECK(shotDay && shotPtera && shotGroup && shotFade && shotNight, "missing states: day%d ptera%d group%d fade%d night%d", shotDay, shotPtera, shotGroup, shotFade, shotNight);

    {   // a ducking dino (the bot prefers jumping, so force it)
        Game k;
        init(k, 31);
        step(k, start);
        for (int i = 0; i < 90; ++i) step(k, NONE_IN);
        Input duck = {false, false, true};
        step(k, duck);
        CHECK(isDucking(k), "duck state");
        drawAndCheck(k, false);
        savePPM("r_duck", FIELD_Y, VIEW_H);
    }

    // game over + paused screens
    Game d;
    init(d, 9);
    step(d, start);
    for (int i = 0; i < 400 && d.phase == PH_RUNNING; ++i) step(d, NONE_IN);       // dies on the first cactus
    CHECK(d.phase == PH_DEAD, "idle dino should crash");
    drawAndCheck(d, false);
    savePPM("r_dead", FIELD_Y, VIEW_H);
    Game p;
    init(p, 9);
    drawAndCheck(p, false);
    savePPM("r_title", FIELD_Y, VIEW_H);
    step(p, start);
    for (int i = 0; i < 30; ++i) step(p, NONE_IN);
    drawAndCheck(p, true);
    savePPM("r_paused", FIELD_Y, VIEW_H);

    // deterministic: same state draws the same pixels
    static uint16_t a[320 * 208];
    drawAndCheck(g, false);
    memcpy(a, g_vram + FIELD_Y * 320, sizeof a);
    drawAndCheck(g, false);
    CHECK(memcmp(a, g_vram + FIELD_Y * 320, sizeof a) == 0, "rendering must be deterministic");

    // timing (host)
    timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    const int N = 2000;
    for (int i = 0; i < N; ++i) render::drawField(g, FIELD_Y, false);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("drawField: %.1f us per frame on this PC\n", ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / N);

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all render tests passed\n");
    return 0;
}
