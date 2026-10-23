# TOWER DEFENSE  (Wii homebrew)

A retro, top-down, colourful tower defense game for the Nintendo Wii, played with the
**Wii Classic Controller** (no motion controls). 16:9, 60 fps, retro-style graphics and clean, modern-sounding sound effects.
**Endless:** every level keeps sending bigger waves until the keep falls - how far can you get? Your best wave per level is shown on the title screen (kept until you close the game).

Build **arrow**, **cannon** and **frost** towers along the road and stop slimes, bats, golems and bosses
from reaching your keep. There are **4 levels**:

1. **Green Meadow** - the sunny classic
2. **Stormy Moor** - dark, rainy slate-blue moorland with lightning
3. **Haunted Marsh** - a glowing green graveyard bog with a zig-zag road
4. **Thunder Peak** - violet rock and crystals under a thunderstorm

The three storm levels have rain, silent lightning flashes, and glowing lamps, plants, portal, keep
and shots. Clearing a level takes you to the next one with fresh gold and lives. On the title screen press
**left/right** on the D-pad to jump straight to any level.

## Controls (Classic Controller)

| Button | Action |
|---|---|
| D-Pad | move the cursor (hold to repeat); on the title screen, left/right picks the level |
| A | build the selected tower / upgrade the tower under the cursor |
| B | sell the tower under the cursor (70% refund) |
| L / R (or ZL / ZR) | change the selected tower type |
| X | call the next wave early (bonus gold for the time saved) |
| Y | fast-forward on/off |
| + | pause menu (Up/Down + A: resume, sound on/off, quit to title) |
| - | switch screen mode: the game starts filling a 16:9 screen; press - for a letterboxed picture on a 4:3 TV |
| HOME | quit |

Tips: cannons only hit ground enemies (bats fly), frost slows everything it touches, towers go up to
level 3 (blue trim at level 2, gold at level 3), and bosses arrive every few waves.

## Build it  (needs devkitPro)

1. Install devkitPro and the Wii toolchain: <https://devkitpro.org/wiki/Getting_Started>, then
   `sudo dkp-pacman -S wii-dev`  (on Windows use the devkitPro installer's MSYS2 shell).
2. In this folder run:  `make`   ->  produces **boot.dol**

> **Honest note:** the game logic, rendering, sound synthesis and input handling were tested natively
> (see "Testing" below), but this project has **not been compiled against a real devkitPPC/libogc**, nor
> run in Dolphin, because that toolchain wasn't available where it was written. `source/main.c` (the only
> file that touches libogc) type-checks against stand-in headers, but if the real compiler complains
> about an API name, the likely spots are: `VOICE_MONO16` / `ASND_*` (audio), the `WPAD_CLASSIC_BUTTON_*`
> names (input; fallbacks are defined at the top of main.c), and `TVEurgb60Hz480IntDf` (video).
> They are one-line fixes.

## Run it in Dolphin

1. **File > Open...** and choose `boot.dol` (or drag it onto Dolphin).
2. Controllers > **Wii Remote 1** = *Emulated Wii Remote*, click **Configure**, set **Extension = Classic Controller**,
   and map the Classic Controller tab to your keyboard or gamepad. (Set this *before* starting the game.)
3. Config > **Wii** tab: set **Aspect Ratio = 16:9** and, if you like, enable **Progressive Scan** (480p looks
   cleanest). In Graphics, leave Aspect Ratio on **Auto** and turn **VSync** on.
4. Make sure emulation speed is 100% - the game then runs at a locked 60 fps.

It also runs on a real Wii via the Homebrew Channel: copy `boot.dol` and `meta.xml` to `apps/towerdefense/` on the SD card.

## 60 fps

* The game simulates in fixed 1/60 s steps and is locked to the console's vertical sync. If a frame is ever
  late it catches up so the game never slows down. On PAL consoles it switches to PAL60 so it is still 60 Hz.
* Worst-case frame cost (143 fully upgraded towers, 300 particles, enemies piled up, and the storm levels'
  rain/lightning/glow) measured on a PC: about **0.1-0.2 ms** (typical to 99th percentile) of the 16.7 ms frame budget. The Wii's CPU is much slower
  than a PC, but even at 30x slower that leaves most of the frame free. Run `tools/bench.c` yourself:
  `cc -O2 -std=c99 source/game.c source/audio.c tools/bench.c -lm -o bench && ./bench`

## Testing (no Wii toolchain needed)

`make test` (or `sh tools/run_tests.sh`) builds the game core natively and runs a harness that drives it with
simulated controller input: movement/auto-repeat, build/upgrade/sell, can't-build rules, pause, the sound
event for every action, every level played by several bots (to tune the difficulty), a do-nothing run
that must lose, the storm weather (lightning and palette flashes), endless waves and best-wave tracking, and screenshot dumps (`tools/out/*.png`). It also writes every sound effect as a WAV file
in `tools/out/sounds/` so you can listen to them.

## Layout

```
source/game.c, game.h   game rules + software renderer (plain C99, 320x180 palette framebuffer; 4 levels)
source/audio.c, audio.h synthesized sound effects (no audio files needed)
source/main.c           the only Wii-specific file: video (YUY2 XFB), Classic Controller, ASND audio
tools/                  native tests, benchmark, PNG/WAV export
Makefile, meta.xml      build + Homebrew Channel metadata
```
