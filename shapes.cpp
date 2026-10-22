#include "shapes.h"
#include "art.h"

namespace shapes {

namespace {

constexpr int MAX_RUNS_BYTES = 16384;
constexpr int MAX_ROWS = 1024;          // total rows over all sprites (+1 per sprite); ~700 used

uint8_t  runPool[MAX_RUNS_BYTES];
uint16_t rowPool[MAX_ROWS];
uint8_t  spanPool[MAX_ROWS * 2];
int runUsed, rowUsed, spanUsed;

Sprite sprites[COUNT];
bool ready;

void build(Id id, const char* const* rows, int w, int h) {
    // Fixed pools: a too-small pool would silently corrupt memory, so refuse loudly instead.
    if (rowUsed + h + 1 > MAX_ROWS || spanUsed + 2 * h > MAX_ROWS * 2 || runUsed + 2 * h * ((w + 1) / 2) > MAX_RUNS_BYTES) {
        for (;;) {}                                     // art grew beyond the pools: bump the sizes above
    }
    uint16_t* rs = &rowPool[rowUsed];
    uint8_t* sl = &spanPool[spanUsed];
    uint8_t* sr = sl + h;
    uint8_t* runs = &runPool[runUsed];
    int n = 0;                                          // bytes written to runs
    for (int y = 0; y < h; ++y) {
        rs[y] = (uint16_t)(n / 2);
        sl[y] = 0xFF;
        sr[y] = 0;
        const char* r = rows[y];
        int x = 0;
        while (x < w) {
            if (r[x] != '#') { ++x; continue; }
            const int x0 = x;
            while (x < w && r[x] == '#') ++x;
            runs[n++] = (uint8_t)x0;
            runs[n++] = (uint8_t)(x - x0);
            if (sl[y] == 0xFF) sl[y] = (uint8_t)x0;
            sr[y] = (uint8_t)(x - 1);
        }
    }
    rs[h] = (uint16_t)(n / 2);
    sprites[id] = Sprite{ (uint8_t)w, (uint8_t)h, rs, runs, sl, sr };
    runUsed += n;
    rowUsed += h + 1;
    spanUsed += h * 2;
}

void buildArt(Id id, const art::Art& a) { build(id, a.rows, a.w, a.h); }

// A disc of radius 9.5 with the same disc, shifted sideways, carved out of it.
void buildMoon(Id id, int phase) {
    constexpr int N = 20;
    static char grid[N][N + 1];
    const char* rows[N];
    const int shift = (phase - 3) * 5;                  // 0 = full moon
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            const int dx = 2 * x - (N - 1), dy = 2 * y - (N - 1);           // doubled, centred
            const int ex = 2 * (x - shift) - (N - 1);
            const bool inDisc = dx * dx + dy * dy <= (N - 1) * (N - 1) + 8;
            const bool carved = shift != 0 && ex * ex + dy * dy <= (N - 1) * (N - 1) + 8;
            grid[y][x] = (inDisc && !carved) ? '#' : '.';
        }
        grid[y][N] = 0;
        rows[y] = grid[y];
    }
    build(id, rows, N, N);
}

}  // namespace

void init() {
    if (ready) return;
    ready = true;
    buildArt(DINO_IDLE, art::DINO_IDLE);
    buildArt(DINO_RUN_A, art::DINO_RUN_A);
    buildArt(DINO_RUN_B, art::DINO_RUN_B);
    buildArt(DINO_BLINK, art::DINO_BLINK);
    buildArt(DINO_DEAD, art::DINO_DEAD);
    buildArt(DINO_DUCK_A, art::DINO_DUCK_A);
    buildArt(DINO_DUCK_B, art::DINO_DUCK_B);
    buildArt(CACTUS_SMALL_A, art::CACTUS_SMALL_A);
    buildArt(CACTUS_SMALL_B, art::CACTUS_SMALL_B);
    buildArt(CACTUS_LARGE_A, art::CACTUS_LARGE_A);
    buildArt(CACTUS_LARGE_B, art::CACTUS_LARGE_B);
    buildArt(PTERA_UP, art::PTERA_UP);
    buildArt(PTERA_DOWN, art::PTERA_DOWN);
    buildArt(CLOUD, art::CLOUD);
    buildArt(STAR_A, art::STAR_A);
    buildArt(STAR_B, art::STAR_B);
    buildArt(RESTART, art::RESTART);
    buildArt(BUMP_A, art::BUMP_A);
    buildArt(BUMP_B, art::BUMP_B);
    for (int p = 0; p < NUM_MOON_PHASES; ++p) buildMoon((Id)(MOON_0 + p), p);
}

const Sprite& get(Id id) { return sprites[id]; }

bool overlap(const Sprite& a, int ax, int ay, const Sprite& b, int bx, int by, int shrink) {
    const int y0 = ay > by ? ay : by;
    const int y1 = (ay + a.h < by + b.h) ? ay + a.h : by + b.h;
    for (int y = y0; y < y1; ++y) {
        const int ra = y - ay, rb = y - by;
        if (a.spanL[ra] == 0xFF || b.spanL[rb] == 0xFF) continue;
        const int al = ax + a.spanL[ra] + shrink, ar = ax + a.spanR[ra] - shrink;
        const int bl = bx + b.spanL[rb] + shrink, br = bx + b.spanR[rb] - shrink;
        if (al <= br && bl <= ar) return true;
    }
    return false;
}

}  // namespace shapes
