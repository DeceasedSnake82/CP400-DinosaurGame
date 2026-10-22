// Host harness: runs the real app (main.cpp, compiled with -Dmain=dino_main) against a
// fake 320x528 LCD and scripted input, and writes screenshots as PPM files.
//
//   make -C test shots      # builds, runs test/script.txt, converts to PNG in test/out/
//
// Script commands (one per line, '#' starts a comment). A "frame" is one pass of the
// app's main loop (the harness counts a frame each time the app has drained its event queue).
//   key NAME            press a key once (UP EXE DOT PLUS MINUS 4 5 EQUALS CLEAR DOWN)
//   hold NAME N         hold UP / DOWN / EXE for N frames: a press event, then a KEY_HELD event every
//                       frame while it is down (like the OS), then a release event, plus key-state polling
//   holdflood NAME N    like hold, but the OS reports KEY_HELD on EVERY GetInput call, for N calls (worst case);
//                       the run fails if the app ever makes more than 8 GetInput calls in one frame
//   holdshot NAME       take a screenshot in the middle of the next `hold`
//   poll on|off         switch Input_GetKeyState polling off to emulate the SDK's broken inline wrapper
//   tap X Y             touch down + up
//   down X Y            touch down only          up        release the touch
//   frames N            let the app run N frames without input
//   shot NAME           save what the LCD shows as out/NAME.ppm
//   time NAME           print how long the frames since the last "frames N" took
//   expect_phase P      abort unless the game is in phase P (see game.phase via extern)
#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <string>
#include <vector>

#undef main
extern int dino_main();

static uint16_t g_vram[320 * 528];
static uint16_t g_lcd[320 * 528];          // what the "panel" shows
static long g_refreshes = 0, g_partial = 0;

static bool flood = false;                              // worst-case mode: KEY_HELD on every GetInput call
static long floodCalls = 0, floodFrames = 0;            // GetInput calls / main-loop passes while a key is held
static int g_calls = 0, g_maxCalls = 0;                 // GetInput calls in the current main-loop pass / the maximum
static void frameEnd() { if (g_calls > g_maxCalls) g_maxCalls = g_calls; g_calls = 0; }
void host_frame_start() { frameEnd(); if (flood) ++floodFrames; }
static void host_refresh() { memcpy(g_lcd, g_vram, sizeof g_lcd); ++g_refreshes; }
void host_present_rows(int y0, int y1) { memcpy(g_lcd + y0 * 320, g_vram + y0 * 320, (size_t)(y1 - y0 + 1) * 320 * 2); ++g_partial; }
static uint16_t* host_vram() { return g_vram; }

static void shot(const char* name) {
    char path[256];
    snprintf(path, sizeof path, "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n320 528\n255\n");
    for (int i = 0; i < 320 * 528; ++i) {
        const uint16_t c = g_lcd[i];
        const uint8_t rgb[3] = { (uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static double now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

struct Cmd { std::string op, a; int n[4]; };
static std::vector<Cmd> script;
static size_t pc = 0;
static int waitFrames = 0, holdFrames = 0;
static uint16_t heldScan = 0;
static int heldCode = 0;
static std::string holdShotName;

static long floodCallsLeft = 0;
static bool holdEmitted = false, pendingRelease = false, pollOn = true;
static struct { int dir, x, y; } pending[8];
static int nPending = 0, iPending = 0;
static bool quitSent = false;
static double timeStart = 0; static int timeFrames = 0; static long timeRefresh = 0;

static int keyCode(const std::string& k) {
    static const struct { const char* n; int c; } T[] = {
        {"UP", KEYCODE_UP}, {"DOWN", KEYCODE_DOWN}, {"EXE", KEYCODE_EXE}, {"DOT", KEYCODE_DOT},
        {"PLUS", KEYCODE_PLUS}, {"MINUS", KEYCODE_MINUS}, {"4", KEYCODE_4}, {"5", KEYCODE_5},
        {"EQUALS", KEYCODE_EQUALS}, {"CLEAR", KEYCODE_POWER_CLEAR} };
    for (auto& t : T) if (k == t.n) return t.c;
    fprintf(stderr, "unknown key %s\n", k.c_str()); exit(1);
}
static int scanCode(const std::string& k) {
    if (k == "UP") return ScancodeUp;
    if (k == "DOWN") return ScancodeDown;
    if (k == "EXE") return ScancodeEXE;
    fprintf(stderr, "unknown scancode %s\n", k.c_str()); exit(1);
}
static void setKey(Input_Event* e, int code, int dir = KEY_PRESSED) {
    e->type = EVENT_KEY; e->data.key.direction = (Input_KeyEventType)dir; e->data.key.keyCode = (Input_Keycode)code;
}
static void setTouch(Input_Event* e, int dir, int x, int y) {
    e->type = EVENT_TOUCH; e->data.touch_single.direction = (Input_TouchEventType)dir;
    e->data.touch_single.p1_x = x; e->data.touch_single.p1_y = y;
}

static int host_getinput(Input_Event* e, uint32_t, uint32_t) {
    e->type = EVENT_NONE;
    ++g_calls;
    if (flood) {
        if (floodCallsLeft-- <= 0) { flood = false; heldScan = 0; pendingRelease = true; }
        else { ++floodCalls; setKey(e, heldCode, KEY_HELD); return 0; }
    }
    if (iPending < nPending) { setTouch(e, pending[iPending].dir, pending[iPending].x, pending[iPending].y); ++iPending; return 0; }
    nPending = iPending = 0;
    if (waitFrames > 0) { --waitFrames; ++timeFrames; return 0; }
    if (holdFrames > 0) {
        if (!holdEmitted) { holdEmitted = true; setKey(e, heldCode, KEY_HELD); return 0; }
        holdEmitted = false;
        if (holdFrames == 3 && !holdShotName.empty()) { shot(holdShotName.c_str()); holdShotName.clear(); }
        ++timeFrames;
        if (--holdFrames == 0) { heldScan = 0; pendingRelease = true; }
        return 0;
    }
    if (pendingRelease) { pendingRelease = false; setKey(e, heldCode, KEY_RELEASED); return 0; }
    while (pc < script.size()) {
        const Cmd& c = script[pc++];
        if (c.op == "key") { setKey(e, keyCode(c.a)); return 0; }
        if (c.op == "hold") { heldScan = (uint16_t)scanCode(c.a); heldCode = keyCode(c.a); holdFrames = c.n[0]; holdEmitted = false; setKey(e, heldCode); return 0; }
        if (c.op == "holdflood") { heldScan = (uint16_t)scanCode(c.a); heldCode = keyCode(c.a); flood = true; floodCallsLeft = c.n[0]; setKey(e, heldCode); return 0; }
        if (c.op == "poll") { pollOn = c.a == "on"; continue; }
        if (c.op == "holdshot") { holdShotName = c.a; continue; }
        if (c.op == "tap") { pending[0] = { TOUCH_DOWN, c.n[0], c.n[1] }; pending[1] = { TOUCH_UP, c.n[0], c.n[1] }; nPending = 2; iPending = 0; return host_getinput(e, 0, 0); }
        if (c.op == "down") { pending[0] = { TOUCH_DOWN, c.n[0], c.n[1] }; nPending = 1; iPending = 0; return host_getinput(e, 0, 0); }
        if (c.op == "up") { pending[0] = { TOUCH_UP, 0, 0 }; nPending = 1; iPending = 0; return host_getinput(e, 0, 0); }
        if (c.op == "frames") { waitFrames = c.n[0]; timeFrames = 0; timeStart = now(); timeRefresh = g_refreshes + g_partial; --waitFrames; ++timeFrames; return 0; }
        if (c.op == "shot") { shot(c.a.c_str()); continue; }
        if (c.op == "time") {
            const double dt = now() - timeStart;
            printf("%-24s %8.3f ms/frame over %d frames (%ld LCD updates)\n", c.a.c_str(), dt * 1000 / (timeFrames ? timeFrames : 1), timeFrames, g_refreshes + g_partial - timeRefresh);
            continue;
        }
        fprintf(stderr, "bad script op %s\n", c.op.c_str()); exit(1);
    }
    if (!quitSent) { quitSent = true; setKey(e, KEYCODE_POWER_CLEAR); }
    return 0;
}

static bool host_keystate(uint16_t* scancode) { return pollOn && heldScan && *scancode == heldScan; }

int (*GetInput)(struct Input_Event*, uint32_t, uint32_t) = host_getinput;
bool (*Input_GetKeyStateU)(uint16_t*) __asm__("Input_GetKeyState") = host_keystate;
void (*LCD_Refresh)() = host_refresh;
uint16_t* (*_FP_LCD_GetVRAMAddress)() __asm__("LCD_GetVRAMAddress") = host_vram;

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "script.txt";
    FILE* f = fopen(path, "r");
    if (!f) { perror(path); return 1; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char op[32] = "", a[64] = ""; int n[4] = {0, 0, 0, 0};
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%31s", op) != 1) continue;
        Cmd c; c.op = op;
        if (c.op == "key" || c.op == "shot" || c.op == "time" || c.op == "poll" || c.op == "holdshot") sscanf(line, "%*s %63s", a);
        else if (c.op == "hold" || c.op == "holdflood") sscanf(line, "%*s %63s %d", a, &n[0]);
        else sscanf(line, "%*s %d %d %d %d", &n[0], &n[1], &n[2], &n[3]);
        c.a = a; memcpy(c.n, n, sizeof n);
        script.push_back(c);
    }
    fclose(f);
    dino_main();
    printf("done: %ld full refreshes, %ld partial; most GetInput calls in one frame: %d\n", g_refreshes, g_partial, g_maxCalls);
    printf("while a key is held: %ld GetInput calls over %ld frames\n", floodCalls, floodFrames);
    if (floodFrames > 0 && floodCalls * 2 > floodFrames) { printf("FAIL: input is read on more than every other frame while a key is held\n"); return 1; }
    if (g_maxCalls > 8) { printf("FAIL: the app drains too many events per frame (%d)\n", g_maxCalls); return 1; }
    return 0;
}
