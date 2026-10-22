# Dino for the CP400

The Chrome "no internet" dinosaur game for the Casio fx-CP400 / ClassPad II, written for
[Hollyhock 3](https://github.com/classpaddev/hollyhock-3). Copy **`Dino.hh3`** to the calculator and start it from the launcher.

## What is in it

* Running T-Rex with run / jump / duck / dead animations, a blinking idle pose, variable jump height and **fast fall**
* Small and large **cacti** in groups of 1-3 (bigger groups only appear at higher speeds), three flight heights of **pterodactyls** (jump the low one, duck the middle one, walk under the high one)
* **Day and night**: every 700 points the world fades to night for a while, with a **moon that changes phase**, twinkling **stars**, drifting clouds and a scrolling ground texture
* Score, **high score saved to a file** (`\\fls0\DinoHi.sav`, a 9-byte checksummed record; a corrupt or foreign file is ignored; if a save or load fails, the header shows `SAVE ERR` / `HI LOAD ERR` with the OS error code), score flashing every 100 points, game-over screen with the restart icon, pause
* Speeds up as you go (4 to 9 pixels per tick); obstacle gaps scale with speed and are proven passable by an automated look-ahead player (see Tests)

## Controls

| Action | Key | Touch |
| --- | --- | --- |
| Jump (hold for a higher jump); start; restart | `UP` or `EXE` | tap the playfield or the green JUMP button |
| Duck, or fall faster in the air | `DOWN` | hold the blue DUCK button |
| Pause | `.` | PAUSE |
| Game speed | `+` / `-` | FASTER / SLOWER |
| Day / night mode (auto, always day, always night) | `4` | NIGHT |
| LCD mode, safe / fast (experimental, see below) | `5` | LCD |
| Help | `=` | HELP |
| Quit | `CLEAR` | |

## Game speed (important)

The Hollyhock SDK has no timer, so the game cannot know how fast it really runs. Each frame is "update + draw + LCD refresh + a busy wait". The **SPEED 1-9** setting
(`+` / `-`) changes the busy wait. It starts at 9, the fastest (no busy wait); lower it if the game is too quick on your calculator.

## "LCD: FAST" (experimental)

`LCD_Refresh()` always pushes the whole 320x528 screen. In FAST mode only the playfield rows are sent through the LCD controller window
(`LCD_SetDrawingBounds` + `lcd_data_port`), about 40% of the data. The SDK does not document those calls in detail, so this is **off by default**: if the screen looks wrong, press `5` again.

## Building

You need the Hollyhock 3 toolchain and SDK; the `ghcr.io/classpaddev/hollyhock-3` image has both (the Chess project's dev container uses it).

```sh
export SDK_DIR=/sdk        # default
make                       # -> Dino.elf and Dino.hh3
make fpucheck              # fails if soft-float code was linked (the CPU has no FPU)
```

GitHub Actions (`.github/workflows/build.yml`) runs the host tests and builds `Dino.hh3` in that image, and uploads it as an artifact.

## Tests (run on the PC, not on the calculator)

```sh
make -C test SDK_DIR=/path/to/hollyhock-3/sdk test    # rules + renderer (needs only the SDK headers)
make -C test SDK_DIR=... shots                        # runs the real UI headlessly, screenshots in test/out/
```

* `test/test_game.cpp`: jump physics, collision rules (every pterodactyl height), spawn invariants (gaps, no triple repeats, pteras only when fast enough),
  determinism, the day/night schedule, the save-file format (round trip, corruption rejected), and a **look-ahead bot** (`test/bot.h`) that plays 380,000 ticks over 16 seeds using only the human controls and must never crash.
* `test/test_render.cpp`: nothing is drawn outside the playfield, rendering is deterministic, screenshots of day / night / fade / groups / pterodactyl / game over.
* `test/host_app.cpp` runs `main.cpp` unchanged against a fake LCD and scripted input (`test/script.txt`).

## Source layout

| File | |
| --- | --- |
| `game.h/.cpp` | the rules: physics, obstacles, score, day/night (fixed point, no SDK) |
| `render.h/.cpp` | game state to pixels |
| `shapes.h/.cpp`, `art.h` | ASCII-art sprites to run-length rows + collision extents |
| `gfx.h/.cpp` | framebuffer helpers and the 5x7 font |
| `save.h/.cpp` | high score file (record format is pure and unit-tested) |
| `main.cpp` | input, touch buttons, panel, pacing, main loop |
| `cp400.md` | field guide for writing CP400 games (SDK, hardware, performance, testing) |

## Notes from testing on a real calculator

* The SDK's `Input_GetKeyState()` is broken on this big-endian CPU (it passes a 4-byte enum where a 16-bit scancode is read, so it always asks about scancode 0). The game calls `Input_GetKeyStateU()` with a real `uint16_t`, and also tracks key press / held / release events as a fallback.
* While a key is down the OS keeps reporting "held" events on every `GetInput` call, so draining the queue until empty made every frame slow while holding a key (jumping with UP). The game now stops at the first held event and never makes more than a few calls per frame; the host harness checks this (`holdflood` in `test/script.txt`).
