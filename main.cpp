// ============================================================================
//  Dino - the Chrome "no internet" game for the CP400 (Hollyhock 3)
//
//  game.cpp    rules (physics, obstacles, score, day/night) - no SDK, fixed point
//  render.cpp  playfield strip -> VRAM
//  shapes.cpp  ASCII art (art.h) -> run-length sprites + collision extents
//  main.cpp    input, touch buttons, panel, pacing, main loop
//
//  Controls (also shown by the HELP button):
//    UP / EXE / tap the field / JUMP button   jump (hold = higher)
//    DOWN / DUCK button                       duck, or fall fast in the air
//    .  pause     + / -  game speed     4  day / night mode
//    6  input debug overlay             =  help          CLEAR  quit
//
//  The LCD is updated with partial window writes only (header / playfield / panel rows that
//  changed); LCD_Refresh() is used once at start-up.
// ============================================================================

#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdint.h>

#include "game.h"
#include "gfx.h"
#include "render.h"
#include "save.h"

APP_NAME("Dino")
APP_DESCRIPTION("The Chrome offline dinosaur game: cacti, pterodactyls, day and night")
APP_AUTHOR("DeceasedSnake82")
APP_VERSION("1.0.0")

using gfx::rgb;

#ifdef DINO_HOST
extern void host_present_rows(int y0, int y1);   // test harness: stands in for the partial LCD update
extern void host_frame_start();                  // test harness: marks the start of a main-loop pass
#endif

namespace {

// ---------------------------------------------------------------------------
// Layout (screen is 320 x 528)
// ---------------------------------------------------------------------------
constexpr int HEADER_H = 40;
constexpr int FIELD_Y  = HEADER_H;
constexpr int PANEL_Y  = FIELD_Y + dino::VIEW_H;           // 248
constexpr int BIG_Y = PANEL_Y + 14, BIG_H = 104;
constexpr int JUMP_X = 8,  JUMP_W = 196;
constexpr int DUCK_X = 212, DUCK_W = 100;
constexpr int SM_Y0 = BIG_Y + BIG_H + 12, SM_H = 44, SM_GAP = 8;
constexpr int SM_W = 100, SM_X0 = 8, SM_DX = 106;
constexpr int INFO_Y = SM_Y0 + 2 * (SM_H + SM_GAP) + 6;

constexpr uint16_t COL_BG    = rgb(24, 26, 32);
constexpr uint16_t COL_BTN   = rgb(58, 64, 84);
constexpr uint16_t COL_EDGE  = rgb(110, 118, 150);
constexpr uint16_t COL_JUMP  = rgb(36, 128, 72);
constexpr uint16_t COL_DUCK  = rgb(60, 96, 170);
constexpr uint16_t COL_TEXT  = rgb(240, 240, 240);
constexpr uint16_t COL_DIM   = rgb(150, 156, 176);
constexpr uint16_t COL_MSG   = rgb(250, 214, 90);
constexpr uint16_t COL_HELP  = rgb(10, 12, 20);

enum Button { B_PAUSE, B_NIGHT, B_QUIT, B_SLOWER, B_FASTER, B_HELP, B_COUNT };

// ---------------------------------------------------------------------------
// Pace. The SDK has no timer, so the speed of the game is set by how long each frame
// takes: LCD refresh + a busy-wait whose length is the "game speed" setting.
// ---------------------------------------------------------------------------
constexpr int MIN_PACE = 1, MAX_PACE = 9, DEFAULT_PACE = 9;     // 9 = no busy-wait: as fast as the LCD allows
constexpr uint32_t SPIN_PER_LEVEL = 14000;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
dino::Game game;
bool quit, paused, helpOpen;
bool panelDirty = true, headerDirty = true, fieldDirty = true;
bool firstPresent = true;                    // the very first frame uses LCD_Refresh (whole screen)
int  pace = DEFAULT_PACE;
char msg[40];

bool evJump;                                 // jump pressed since the last tick (key or touch)
bool touchJump, touchDuck;                   // finger is down on the field/JUMP, on DUCK
// "Key is down", in frames left. Refreshed whenever the key-state poll or a press / held event
// sees the key down; a release event ends it at once (unless the poll still sees the key), and
// otherwise it runs out after HOLD_FRAMES, which bridges a frame or two where the OS reports nothing
// (a hold that drops out makes the jump short).
constexpr int HOLD_FRAMES = 3;
int jumpEvt, duckEvt;
bool jumpSeenEv, jumpRelEv, duckSeenEv, duckRelEv;   // what this frame's events said about the keys
// Input debug overlay (key 6): what the OS reports for the jump key.
bool debugInput;
bool dbgPollUp;
int  dbgHeldEvents, dbgReleaseEvents, dbgPressEvents;

void setMsg(const char* s) { gfx::putStr(msg, s); headerDirty = true; }

const char* nightName() {
    return game.nightMode == dino::NIGHT_AUTO ? "AUTO" : (game.nightMode == dino::NIGHT_OFF ? "DAY" : "NIGHT");
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------
void actPause() {
    if (game.phase != dino::PH_RUNNING) return;
    paused = !paused;
    fieldDirty = panelDirty = true;
    setMsg(paused ? "PAUSED" : "GO!");
}

void actNight() {
    game.nightMode = (dino::NightMode)((game.nightMode + 1) % 3);
    if (game.nightMode == dino::NIGHT_AUTO) game.nightTimer = 0;
    fieldDirty = panelDirty = true;
    char b[32];
    gfx::putStr(gfx::putStr(b, "NIGHT MODE: "), nightName());
    setMsg(b);
}

void actDebug() {
    debugInput = !debugInput;
    dbgHeldEvents = dbgReleaseEvents = dbgPressEvents = 0;
    headerDirty = true;
}

void actPace(int d) {
    const int p = pace + d;
    if (p < MIN_PACE || p > MAX_PACE) return;
    pace = p;
    panelDirty = true;
    char b[32];
    gfx::putInt(gfx::putStr(b, "GAME SPEED "), pace);
    setMsg(b);
}

void actHelp() { helpOpen = true; fieldDirty = true; }

void runAction(int id) {
    switch (id) {
        case B_PAUSE:  actPause();  break;
        case B_NIGHT:  actNight();  break;
        case B_QUIT:   quit = true; break;
        case B_SLOWER: actPace(-1); break;
        case B_FASTER: actPace(+1); break;
        case B_HELP:   actHelp();   break;
    }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
void bigButton(int x, int w, uint16_t col, const char* label) {
    gfx::fillRect(x, BIG_Y, w, BIG_H, col);
    gfx::frameRect(x, BIG_Y, w, BIG_H, COL_EDGE);
    gfx::text(x + (w - gfx::textWidth(label, 3)) / 2, BIG_Y + (BIG_H - 21) / 2, label, 3, COL_TEXT);
}

void smallButtonRect(int id, int& x, int& y) {
    x = SM_X0 + (id % 3) * SM_DX;
    y = SM_Y0 + (id / 3) * (SM_H + SM_GAP);
}

const char* buttonLabel(int id) {
    switch (id) {
        case B_PAUSE:  return paused ? "RESUME" : "PAUSE";
        case B_NIGHT:  return "NIGHT";
        case B_QUIT:   return "QUIT";
        case B_SLOWER: return "SLOWER";
        case B_FASTER: return "FASTER";
        default:       return "HELP";
    }
}

void drawPanel() {
    gfx::fillRect(0, PANEL_Y, gfx::W, gfx::H - PANEL_Y, COL_BG);
    bigButton(JUMP_X, JUMP_W, COL_JUMP, "JUMP");
    bigButton(DUCK_X, DUCK_W, COL_DUCK, "DUCK");
    for (int id = 0; id < B_COUNT; ++id) {
        int x, y;
        smallButtonRect(id, x, y);
        gfx::fillRect(x, y, SM_W, SM_H, COL_BTN);
        gfx::frameRect(x, y, SM_W, SM_H, COL_EDGE);
        const char* s = buttonLabel(id);
        gfx::text(x + (SM_W - gfx::textWidth(s, 2)) / 2, y + (SM_H - 14) / 2, s, 2, COL_TEXT);
    }
    char b[48];
    char* p = gfx::putInt(gfx::putStr(b, "SPD "), pace);
    gfx::putStr(gfx::putStr(p, "  NIGHT "), nightName());
    gfx::text(8, INFO_Y, b, 2, COL_DIM);
    gfx::text(8, INFO_Y + 22, "UP JUMP  DOWN DUCK  CLEAR QUIT", 1, COL_DIM);
}

void drawHeader() {
    gfx::fillRect(0, 0, gfx::W, HEADER_H, COL_BG);
    if (debugInput) {
        // P: key-state poll sees UP/EXE   H: jump hold in frames   then counts of press / held / release events
        char b[48];
        char* p = gfx::putInt(gfx::putStr(b, "P"), dbgPollUp ? 1 : 0);
        p = gfx::putInt(gfx::putStr(p, " H"), jumpEvt);
        p = gfx::putInt(gfx::putStr(p, " DN"), dbgPressEvents);
        p = gfx::putInt(gfx::putStr(p, " HLD"), dbgHeldEvents);
        gfx::putInt(gfx::putStr(p, " REL"), dbgReleaseEvents);
        gfx::text(8, 13, b, 2, COL_MSG);
        return;
    }
    gfx::text(8, 13, msg[0] ? msg : "DINO RUN", 2, msg[0] ? COL_MSG : COL_TEXT);
}

void drawHelp() {
    static const char* const LINES[] = {
        "DINO RUN - CONTROLS",
        "UP/EXE/TAP  JUMP (HOLD)",
        "DOWN   DUCK / FAST FALL",
        ".      PAUSE",
        "+ -    GAME SPEED",
        "4      DAY / NIGHT MODE",
        "6      INPUT DEBUG",
        "=      HELP   CLEAR  QUIT",
        "DUCK UNDER LOW PTEROS,",
        "JUMP THE REST.  TAP: CLOSE",
    };
    gfx::fillRect(0, FIELD_Y, gfx::W, dino::VIEW_H, COL_HELP);
    int y = FIELD_Y + 10;
    int n = 0;
    for (const char* l : LINES) {
        gfx::text(8, y, l, 2, n == 0 ? COL_MSG : COL_TEXT);
        y += 19;
        ++n;
    }
}

// Sends the VRAM rows y0..y1 (full width) to the LCD through the controller window, which is
// much less data than LCD_Refresh() (the whole 320x528 screen). The first frame of the app uses
// LCD_Refresh() once so every pixel is known to be on the panel.
void pushRows(int y0, int y1) {
#ifdef DINO_HOST
    host_present_rows(y0, y1);
#else
    LCD_SetDrawingBounds(0, gfx::W - 1, y0, y1);
    LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
    const uint16_t* p = gfx::fb + y0 * gfx::W;
    int n = (y1 - y0 + 1) * gfx::W;                      // a multiple of 8: 320 pixels per row
    for (; n >= 8; n -= 8, p += 8) {
        *lcd_data_port = p[0]; *lcd_data_port = p[1]; *lcd_data_port = p[2]; *lcd_data_port = p[3];
        *lcd_data_port = p[4]; *lcd_data_port = p[5]; *lcd_data_port = p[6]; *lcd_data_port = p[7];
    }
#endif
}

void spin() {
#ifndef DINO_HOST
    volatile uint32_t n = (uint32_t)(MAX_PACE - pace) * SPIN_PER_LEVEL;
    while (n) n = n - 1;
#endif
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Do NOT use the SDK's inline Input_GetKeyState(): it hands the address of a 4-byte enum to a
// function that reads a uint16_t, and on this big-endian CPU that reads the upper half, which
// is always 0 - so it asks about scancode 0 and never sees the key. Pass a real uint16_t.
bool keyDown(Input_Scancode sc) {
    uint16_t code = (uint16_t)sc;
    return Input_GetKeyStateU(&code);
}

void onTouch(int dir, int x, int y) {
    if (dir == TOUCH_UP) { touchJump = touchDuck = false; return; }
    if (dir != TOUCH_DOWN) return;
    msg[0] = 0;
    headerDirty = true;
    if (helpOpen) { helpOpen = false; fieldDirty = true; return; }
    if (y >= FIELD_Y && y < PANEL_Y) { evJump = touchJump = true; return; }
    if (y >= BIG_Y && y < BIG_Y + BIG_H) {
        if (x >= JUMP_X && x < JUMP_X + JUMP_W) evJump = touchJump = true;
        else if (x >= DUCK_X && x < DUCK_X + DUCK_W) touchDuck = true;
        return;
    }
    for (int id = 0; id < B_COUNT; ++id) {
        int bx, by;
        smallButtonRect(id, bx, by);
        if (x >= bx && x < bx + SM_W && y >= by && y < by + SM_H) { runAction(id); return; }
    }
}

void onKey(int code) {
    msg[0] = 0;
    headerDirty = true;
    if (helpOpen) {
        helpOpen = false;
        fieldDirty = true;
        if (code != KEYCODE_POWER_CLEAR) return;
    }
    switch (code) {
        case KEYCODE_POWER_CLEAR: quit = true; break;
        case KEYCODE_UP:
        case KEYCODE_EXE:         evJump = true; break;
        case KEYCODE_DOT:         actPause(); break;
        case KEYCODE_PLUS:        actPace(+1); break;
        case KEYCODE_MINUS:       actPace(-1); break;
        case KEYCODE_4:           actNight(); break;
        case KEYCODE_6:           actDebug(); break;
        case KEYCODE_EQUALS:      actHelp(); break;
        default: break;
    }
}

// Any key event: remembers what the events said about the jump / duck keys this frame (the last
// event wins), then handles fresh presses.
void onKeyEvent(int dir, int code) {
    const bool down = dir != KEY_RELEASED;
    const bool jumpKey = code == KEYCODE_UP || code == KEYCODE_EXE;
    if (jumpKey) {
        jumpSeenEv = down; jumpRelEv = !down;
        if (dir == KEY_PRESSED) ++dbgPressEvents; else if (dir == KEY_HELD) ++dbgHeldEvents; else ++dbgReleaseEvents;
    } else if (code == KEYCODE_DOWN) {
        duckSeenEv = down; duckRelEv = !down;
    }
    if (dir == KEY_PRESSED) onKey(code);
}

// Drains the event queue. When `wait` is set the first call blocks (nothing is animating).
// The OS keeps reporting KEY_HELD / TOUCH_HOLD_DRAG for as long as a key / finger is down, so the
// drain stops at the first one (and after a few calls): looping until "empty" made every frame slow.
constexpr int MAX_EVENTS_PER_FRAME = 4;
void pollEvents(bool wait) {
    for (int n = 0; n < MAX_EVENTS_PER_FRAME; ++n) {
        struct Input_Event ev;
        uint8_t* raw = (uint8_t*)&ev;
        for (unsigned i = 0; i < sizeof ev; ++i) raw[i] = 0;
        GetInput(&ev, (n == 0 && wait) ? 0xFFFFFFFF : 0, 0x10);
        if (ev.type == EVENT_KEY) {
            onKeyEvent(ev.data.key.direction, ev.data.key.keyCode);
            if (ev.data.key.direction == KEY_HELD) break;
        } else if (ev.type == EVENT_TOUCH) {
            onTouch(ev.data.touch_single.direction, ev.data.touch_single.p1_x, ev.data.touch_single.p1_y);
            if (ev.data.touch_single.direction == TOUCH_HOLD_DRAG) break;
        } else {
            break;
        }
        if (quit) break;
    }
}

}  // namespace

int main() {
    gfx::init();
    dino::init(game, 0x1BADB002u);
    uint32_t saved;
    const int loadErr = save::load(saved);
    if (loadErr == 0) game.hiScore = saved;

    setMsg("PRESS UP OR TAP TO START");
    if (loadErr != 0 && loadErr != -14 && loadErr != -8 && loadErr != save::ERR_BAD_RECORD) {   // not just "no file yet"
        char b[40];
        gfx::putInt(gfx::putStr(b, "HI LOAD ERR "), loadErr);
        setMsg(b);
    }
    drawHeader();
    drawPanel();
    headerDirty = panelDirty = false;

    while (!quit) {
#ifdef DINO_HOST
        host_frame_start();
#endif
        const bool idleBlock = paused || helpOpen;
        jumpSeenEv = jumpRelEv = duckSeenEv = duckRelEv = false;
        pollEvents(idleBlock);
        if (quit) break;
        dbgPollUp = keyDown(ScancodeUp) || keyDown(ScancodeEXE);
        const bool pollDown = keyDown(ScancodeDown);
        if (dbgPollUp || jumpSeenEv) jumpEvt = HOLD_FRAMES; else if (jumpRelEv) jumpEvt = 0; else if (jumpEvt > 0) --jumpEvt;
        if (pollDown || duckSeenEv)  duckEvt = HOLD_FRAMES; else if (duckRelEv) duckEvt = 0; else if (duckEvt > 0) --duckEvt;
        if (debugInput) headerDirty = true;

        dino::Input in;
        in.jumpPress = evJump;
        in.jumpHeld  = jumpEvt > 0 || touchJump;
        in.duckHeld  = duckEvt > 0 || touchDuck;
        evJump = false;

        const bool animating = !paused && !helpOpen && game.phase != dino::PH_DEAD;
        if (!paused && !helpOpen) {
            const dino::Phase before = game.phase;
            dino::step(game, in);
            if (before != dino::PH_RUNNING && game.phase == dino::PH_RUNNING) { msg[0] = 0; headerDirty = true; }
            if (before == dino::PH_RUNNING && game.phase == dino::PH_DEAD) {
                if (game.newHigh) {
                    const int err = save::store(game.hiScore);
                    char b[40];
                    if (err == 0) gfx::putStr(b, "NEW HIGH SCORE!");
                    else gfx::putInt(gfx::putStr(b, "NEW HIGH! SAVE ERR "), err);
                    setMsg(b);
                } else {
                    setMsg("TAP OR PRESS UP TO RETRY");
                }
                game.newHigh = false;
            }
        }

        const bool needHeader = headerDirty, needPanel = panelDirty;
        if (needHeader) { drawHeader(); headerDirty = false; }
        if (needPanel)  { drawPanel();  panelDirty = false; }
        bool fieldChanged = false;
        if (helpOpen) {
            if (fieldDirty) { drawHelp(); fieldDirty = false; fieldChanged = true; }
        } else if (animating || fieldDirty) {
            render::drawField(game, FIELD_Y, paused);
            fieldDirty = false;
            fieldChanged = true;
        }
        if (firstPresent) {
            LCD_Refresh();
            firstPresent = false;
        } else {
            if (needHeader)   pushRows(0, HEADER_H - 1);
            if (needPanel)    pushRows(PANEL_Y, gfx::H - 1);
            if (fieldChanged) pushRows(FIELD_Y, FIELD_Y + dino::VIEW_H - 1);
        }
        if (!idleBlock) spin();
    }
    return 0;
}
