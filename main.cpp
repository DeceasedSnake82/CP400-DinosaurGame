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
//    5  LCD mode (safe / fast)          =  help          CLEAR  quit
// ============================================================================

#include <appdef.h>
#include <sdk/calc/calc.h>
#include <sdk/os/lcd.h>
#include <sdk/os/input.h>
#include <stdint.h>

#include "game.h"
#include "gfx.h"
#include "render.h"

APP_NAME("Dino")
APP_DESCRIPTION("The Chrome offline dinosaur game: cacti, pterodactyls, day and night")
APP_AUTHOR("DeceasedSnake82")
APP_VERSION("1.0.0")

using gfx::rgb;

#ifdef DINO_HOST
extern void host_present_rows(int y0, int y1);   // test harness: stands in for the partial LCD update
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

enum Button { B_PAUSE, B_NIGHT, B_LCD, B_SLOWER, B_FASTER, B_HELP, B_COUNT };

// ---------------------------------------------------------------------------
// Pace. The SDK has no timer, so the speed of the game is set by how long each frame
// takes: LCD refresh + a busy-wait whose length is the "game speed" setting.
// ---------------------------------------------------------------------------
constexpr int MIN_PACE = 1, MAX_PACE = 9, DEFAULT_PACE = 6;
constexpr uint32_t SPIN_PER_LEVEL = 14000;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
dino::Game game;
bool quit, paused, helpOpen, fastLcd;
bool panelDirty = true, headerDirty = true, fieldDirty = true, fullRefresh = true;
int  pace = DEFAULT_PACE;
char msg[40];

bool evJump;                                 // jump pressed since the last tick (key or touch)
bool touchJump, touchDuck;                   // finger is down on the field/JUMP, on DUCK

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

void actLcd() {
    fastLcd = !fastLcd;
    fullRefresh = fieldDirty = panelDirty = true;
    setMsg(fastLcd ? "LCD: FAST (EXPERIMENTAL)" : "LCD: SAFE");
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
        case B_LCD:    actLcd();    break;
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
        case B_LCD:    return "LCD";
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
    p = gfx::putStr(gfx::putStr(p, " NIGHT "), nightName());
    gfx::putStr(gfx::putStr(p, " LCD "), fastLcd ? "FAST" : "SAFE");
    gfx::text(8, INFO_Y, b, 2, COL_DIM);
    gfx::text(8, INFO_Y + 22, "UP JUMP  DOWN DUCK  CLEAR QUIT", 1, COL_DIM);
}

void drawHeader() {
    gfx::fillRect(0, 0, gfx::W, HEADER_H, COL_BG);
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
        "5      LCD SAFE / FAST",
        "=      HELP   CLEAR  QUIT",
        "DUCK UNDER LOW PTEROS,",
        "JUMP THE REST. TAP TO CLOSE",
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

// Pushes the screen to the LCD. Safe mode = the OS routine (whole screen). Fast mode
// writes only the playfield rows through the LCD controller window; it is opt-in
// because it relies on the LCD window semantics that the SDK does not document.
void present(bool fieldOnly) {
#ifdef DINO_HOST
    if (fastLcd && fieldOnly && !fullRefresh) { host_present_rows(FIELD_Y, FIELD_Y + dino::VIEW_H - 1); return; }
#else
    if (fastLcd && fieldOnly && !fullRefresh) {
        constexpr int y0 = FIELD_Y, y1 = FIELD_Y + dino::VIEW_H - 1;
        LCD_SetDrawingBounds(0, gfx::W - 1, y0, y1);
        LCD_SendCommand(COMMAND_PREPARE_FOR_DRAW_DATA);
        const uint16_t* p = gfx::fb + y0 * gfx::W;
        for (int n = dino::VIEW_H * gfx::W; n > 0; --n) *lcd_data_port = *p++;
        return;
    }
#endif
    LCD_Refresh();
    fullRefresh = false;
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
        case KEYCODE_5:           actLcd(); break;
        case KEYCODE_EQUALS:      actHelp(); break;
        default: break;
    }
}

// Drains the event queue. When `wait` is set the first call blocks (nothing is animating).
void pollEvents(bool wait) {
    for (int n = 0; n < 32; ++n) {
        struct Input_Event ev;
        uint8_t* raw = (uint8_t*)&ev;
        for (unsigned i = 0; i < sizeof ev; ++i) raw[i] = 0;
        GetInput(&ev, (n == 0 && wait) ? 0xFFFFFFFF : 0, 0x10);
        if (ev.type == EVENT_KEY) {
            if (ev.data.key.direction == KEY_PRESSED) onKey(ev.data.key.keyCode);
        } else if (ev.type == EVENT_TOUCH) {
            onTouch(ev.data.touch_single.direction, ev.data.touch_single.p1_x, ev.data.touch_single.p1_y);
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

    setMsg("PRESS UP OR TAP TO START");
    drawHeader();
    drawPanel();
    headerDirty = panelDirty = false;

    while (!quit) {
        const bool idleBlock = paused || helpOpen;
        pollEvents(idleBlock);
        if (quit) break;

        const bool keyUp = Input_GetKeyState(ScancodeUp) || Input_GetKeyState(ScancodeEXE);
        dino::Input in;
        in.jumpPress = evJump;
        in.jumpHeld  = keyUp || touchJump;
        in.duckHeld  = Input_GetKeyState(ScancodeDown) || touchDuck;
        evJump = false;

        const bool animating = !paused && !helpOpen && game.phase != dino::PH_DEAD;
        if (!paused && !helpOpen) {
            const dino::Phase before = game.phase;
            dino::step(game, in);
            if (before != dino::PH_RUNNING && game.phase == dino::PH_RUNNING) { msg[0] = 0; headerDirty = true; }
            if (before == dino::PH_RUNNING && game.phase == dino::PH_DEAD) {
                setMsg(game.newHigh ? "NEW HIGH SCORE!" : "TAP OR PRESS UP TO RETRY");
                game.newHigh = false;
            }
        }

        const bool chrome = headerDirty || panelDirty;
        if (headerDirty) { drawHeader(); headerDirty = false; }
        if (panelDirty)  { drawPanel();  panelDirty = false; }
        if (helpOpen) {
            if (fieldDirty) { drawHelp(); fieldDirty = false; present(false); }
        } else if (animating || fieldDirty || chrome) {
            render::drawField(game, FIELD_Y, paused);
            fieldDirty = false;
            present(!chrome);
        }
        if (!idleBlock) spin();
    }
    return 0;
}
