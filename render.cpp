#include "render.h"
#include "gfx.h"

namespace render {

using namespace dino;

namespace {

constexpr int GROUND_PERIOD = 640;

struct Decor { uint16_t x; uint8_t dy, len; };           // len 0: bump sprite (dy picks the shape)
constexpr int NUM_DECOR = 34;
Decor decor[NUM_DECOR];
bool decorReady;

uint16_t* base;                                           // top-left pixel of the strip

void makeDecor() {
    decorReady = true;
    uint32_t r = 0x1234ABCDu;
    for (int i = 0; i < NUM_DECOR; ++i) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        Decor& d = decor[i];
        d.x = (uint16_t)((i * GROUND_PERIOD) / NUM_DECOR + (r % 12));
        const uint32_t kind = (r >> 8) % 8;
        if (kind == 0) { d.len = 0; d.dy = (uint8_t)((r >> 16) & 1); }
        else { d.len = (uint8_t)(2 + (r >> 12) % 7); d.dy = (uint8_t)(3 + (r >> 20) % 9); }
    }
}

uint16_t lerp565(uint16_t a, uint16_t b, int f /* 0..256 */) {
    const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
    const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
    const int r = ar + (((br - ar) * f) >> 8);
    const int g = ag + (((bg - ag) * f) >> 8);
    const int bl = ab + (((bb - ab) * f) >> 8);
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

void blit(const shapes::Sprite& s, int x, int y, uint16_t c) {
    int r0 = 0, r1 = s.h;
    if (y < 0) r0 = -y;
    if (y + r1 > VIEW_H) r1 = VIEW_H - y;
    for (int r = r0; r < r1; ++r) {
        uint16_t* row = base + (y + r) * VIEW_W;
        const uint8_t* p = s.runs + 2 * s.rowStart[r];
        const uint8_t* e = s.runs + 2 * s.rowStart[r + 1];
        for (; p < e; p += 2) {
            int x0 = x + p[0], x1 = x0 + p[1];
            if (x0 < 0) x0 = 0;
            if (x1 > VIEW_W) x1 = VIEW_W;
            for (; x0 < x1; ++x0) row[x0] = c;
        }
    }
}

void hspan(int x0, int x1, int y, uint16_t c) {
    if (y < 0 || y >= VIEW_H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > VIEW_W) x1 = VIEW_W;
    if (x1 > x0) { uint16_t* row = base + y * VIEW_W; for (int x = x0; x < x1; ++x) row[x] = c; }
}

void drawGround(const Game& g, const Palette& pal) {
    hspan(0, VIEW_W, GROUND_Y, pal.ink);
    const int scroll = (int)(g.groundScroll >> 16);
    for (int i = 0; i < NUM_DECOR; ++i) {
        const Decor& d = decor[i];
        int x = (int)d.x - scroll;
        if (x < -24) x += GROUND_PERIOD;
        if (x >= VIEW_W) continue;
        if (d.len == 0) {
            const shapes::Sprite& s = shapes::get(d.dy ? shapes::BUMP_B : shapes::BUMP_A);
            blit(s, x, GROUND_Y + 1 - s.h, pal.ink);
        } else {
            hspan(x, x + d.len, GROUND_Y + d.dy, pal.soft);
        }
    }
}

void drawObstacle(const Obstacle& o, const Palette& pal) {
    const int x = (int)(o.x >> 16);
    if (x >= VIEW_W || x + o.w <= 0) return;
    if (o.kind == K_PTERA) {
        blit(shapes::get(o.frame ? shapes::PTERA_DOWN : shapes::PTERA_UP), x, o.y, pal.ink);
        return;
    }
    int cx = x;
    for (int i = 0; i < o.count; ++i) {
        const bool b = (o.variants >> i) & 1;
        const shapes::Id id = o.kind == K_SMALL ? (b ? shapes::CACTUS_SMALL_B : shapes::CACTUS_SMALL_A)
                                                : (b ? shapes::CACTUS_LARGE_B : shapes::CACTUS_LARGE_A);
        const shapes::Sprite& s = shapes::get(id);
        blit(s, cx, GROUND_Y - s.h, pal.ink);
        cx += s.w + 1;
    }
}

void drawScore(const Game& g, const Palette& pal) {
    char buf[24];
    // Right-aligned: "HI 00000 00000"
    auto digits = [](char* d, uint32_t v) { for (int i = 4; i >= 0; --i) { d[i] = (char)('0' + v % 10); v /= 10; } d[5] = 0; };
    char* p = buf;
    *p++ = 'H'; *p++ = 'I'; *p++ = ' ';
    digits(p, g.hiScore);
    const int hiW = gfx::textWidth(buf, 2);
    const int curW = gfx::textWidth("00000", 2);
    const int right = VIEW_W - 8;
    const int curX = right - curW;
    const int hiX = curX - 14 - hiW;
    const int oy = (int)((base - gfx::fb) / VIEW_W);
    if (g.hiScore || g.phase != PH_IDLE) gfx::text(hiX, oy + 8, buf, 2, pal.soft);

    bool show = true;
    uint32_t v = g.score;
    if (g.flashTicks) { v = g.flashScore; show = ((g.flashTicks / 15) & 1) == 0; }
    if (show) {
        digits(buf, v);
        gfx::text(curX, oy + 8, buf, 2, pal.ink);
    }
}

void centerText(const char* s, int scale, int y, uint16_t c) {
    const int oy = (int)((base - gfx::fb) / VIEW_W);
    gfx::text((VIEW_W - gfx::textWidth(s, scale)) / 2, oy + y, s, scale, c);
}

}  // namespace

Palette palette(uint8_t fade) {
    constexpr uint16_t DAY_BG = gfx::rgb(247, 247, 247), DAY_INK = gfx::rgb(83, 83, 83);
    constexpr uint16_t NIGHT_BG = gfx::rgb(10, 10, 12), NIGHT_INK = gfx::rgb(172, 172, 172);
    const int f = fade + (fade >> 7);                       // 0..256
    Palette p;
    p.bg = lerp565(DAY_BG, NIGHT_BG, f);
    p.ink = lerp565(DAY_INK, NIGHT_INK, f);
    p.soft = lerp565(p.bg, p.ink, 150);
    p.faint = lerp565(p.bg, p.ink, 70);
    return p;
}

void drawField(const Game& g, int oy, bool paused) {
    if (!decorReady) makeDecor();
    base = gfx::fb + oy * VIEW_W;
    const Palette pal = palette(g.fade);

    gfx::fillSpan(base, VIEW_W * VIEW_H, pal.bg);

    if (g.fade) {
        const uint16_t sky = lerp565(pal.bg, pal.ink, 40 + (g.fade * 200 >> 8));
        for (const Star& s : g.stars) {
            const bool big = ((s.phase / 30) & 1) != 0;
            blit(shapes::get(big ? shapes::STAR_B : shapes::STAR_A), (int)(s.x >> 16), s.y, sky);
        }
        blit(shapes::get((shapes::Id)(shapes::MOON_0 + g.moonPhase)), (int)(g.moonX >> 16), 16, sky);
    }
    for (int i = 0; i < g.nClouds; ++i) blit(shapes::get(shapes::CLOUD), (int)(g.clouds[i].x >> 16), g.clouds[i].y, pal.faint);

    drawGround(g, pal);
    for (int i = 0; i < g.nObs; ++i) drawObstacle(g.obs[i], pal);
    blit(shapes::get(dinoSprite(g)), DINO_X, dinoTop(g), pal.ink);

    drawScore(g, pal);

    if (g.phase == PH_DEAD) {
        centerText("GAME OVER", 3, 54, pal.ink);
        const shapes::Sprite& r = shapes::get(shapes::RESTART);
        blit(r, (VIEW_W - r.w) / 2, 96, pal.ink);
    } else if (paused) {
        centerText("PAUSED", 3, 60, pal.ink);
    }
}

}  // namespace render
