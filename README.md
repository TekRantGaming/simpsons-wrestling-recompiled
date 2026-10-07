# The Simpsons Wrestling Recompiled

The Simpsons Wrestling (PlayStation, USA, SLUS-01227) recompiled into a native
Windows and Linux program with [psxrecomp](https://github.com/mstan/psxrecomp),
started from the shared [TRG Launcher](https://github.com/TekRantGaming/trg-launcher).

No game data is included. You need your own copy of the game as a `.cue`/`.bin`
disc image of the USA release.

## Features

| Feature | What it does |
| --- | --- |
| **16:9 widescreen** | Matches render wider than 4:3 with real extra picture: the arena and crowd fill the sides, nothing is culled at the new edges, and the HUD (portraits, health and power bars, TAUNT, win trophies) sits at the screen edges. Menus, the title screen, character select and loading screens keep the original 4:3 picture. |
| **HD rendering with smooth outlines** | The game is drawn at up to 8K and scaled down to your window (supersampling), then FXAA, so the characters' black outlines and every other edge are smooth. Stable geometry removes the PlayStation's polygon wobble. |
| **Higher frame rate at normal game speed** | Matches run at up to a real 60 FPS (the original runs at 20-30). The game already scales its movement by the time each frame took, so it plays at exactly the original speed. If your PC cannot draw 60 FPS, the frame rate drops instead of the game slowing down. |
| **Intro skipped** | Boots straight to the title screen: no copyright card, Fox Interactive or Big Ape logo movies. |
| **Everything unlocked** | All wrestlers (Bumblebee Man, Moe, Frink, Flanders...), the Defender and Champion circuits, and Bonus Match Up. |

Every feature can be switched off in the launcher.

## Playing

1. Unzip the release and run `SimpsonsWrestling.exe` (Linux: `./SimpsonsWrestling`).
2. On the **Game** page, choose the `.cue` file of your disc (or drop it on the
   window). The launcher checks that it is the USA disc.
3. Press **PLAY**.

The launcher's settings are kept in `launcher.txt`, the renderer's in
`settings.toml`, and memory-card saves in `saves/`, all next to the program.
Hold **Shift** while starting it to show the launcher if you have hidden it.

## Launcher pages

| Page | Settings |
| --- | --- |
| Game | Your disc image, checked against the USA disc's size |
| Display | Window mode (windowed, borderless, exclusive), window size, widescreen, VSync |
| Graphics | Render resolution (240p to 8K, or your screen's), smooth outlines, smooth textures, stable geometry, sharpening, brightness |
| Gameplay | Frame rate (30 or 60), skip intro, unlock everything, frame-rate counter |
| About | Show the launcher at startup, open the game folder, reset all settings |

## How the features work

All game-specific code is in [`simpsons_mods.c`](simpsons_mods.c), a trusted
psxrecomp plugin (package `mods/preloaded/packages/simpsons.pc`). It never
patches the game's code; it reads the game's own state and the launcher's
switches.

- **Match detection.** The mode byte at `0x8007398C` is 0 in a match (and
  during boot), and `0x800732E4` is set once the match's wrestlers exist. Both
  together gate widescreen and the frame rate.
- **Widescreen** uses psxrecomp's native-wide renderer
  (`psx_mod_set_fixed_display_aspect(16, 9)`) with a world-scene predicate, so
  only matches go wide.
- **Edge culling.** The arena is drawn cell by cell (`0x80054714`): each cell's
  centre is projected and the cell skipped unless `-100 <= SX < 612`, a 100 px
  margin around the 512-wide screen. In 16:9 that left 15 px, so crowd blocks,
  floor and props vanished while still on screen. `game.toml
  [widescreen.cull]` widens both immediates (`0x8005496C`, `0x80054974`) by the
  live reveal plus a 64 px guard; at 4:3 they are unchanged.
- **HUD at the edges.** The game links its HUD into the last slots of each
  frame's ordering table: slot 2038 holds both players' panels, slot 2047 the
  TAUNT labels (y 166) and win trophies (y 20) along with centred text. At
  DrawOTag (`0x8005E0CC`) the plugin tags those packets left or right
  (`psx_mod_tag_hud_primitive`); centred text such as the round banner and
  DEMO stays put. The bar outlines are polylines, so this port also taught
  psxrecomp's renderer to move tagged polylines (they were never shifted).
- **60 FPS.** The main loop (`0x80044F1C`) counts the VBlanks since the last
  frame (`0x80044F80`) and moves everything by a step from a linear table
  (`0x8006ECFC`, 68 per VBlank). A match runs at 20-30 FPS only because a frame
  costs more than one VBlank of R3000A time. During matches the plugin
  overclocks the emulated CPU up to 300% (`psx_mod_set_cpu_overclock`, added
  to the framework for this port), so every frame takes one VBlank: the
  game's own timestep stays 1 and its speed is unchanged. Boot, menus and
  loading keep stock CPU timing.
- **Frame-rate governor.** Drawing twice the frames costs the PC twice the
  renderer time. Every quarter second the plugin compares game time with real
  time; if the game falls behind it lowers the overclock (the frame rate drops
  toward the original 20-30), and it raises it again when there is headroom.
  `SIMPSONS_GOVERNOR_LOG=<file>` logs the level, game FPS and speed every 2 s.
- **Skip intro.** Entry hooks (`game.toml` `mod_function_entry_funcs`) end the
  copyright card's 5-second loop in the boot routine (`0x8001D0F8`) and return
  straight from PlayMovie (`0x80044628`) for the two logo movies. The memory
  card check stays, because it loads your save.
- **Unlocks.** Each VBlank the plugin sets the circuit and Bonus Match Up flags
  (`0x80072BF0`, `0x80072BF2`, `0x80072BD0`) and clears the five hidden
  wrestlers' lock words (`0x8006DCE4`..`0x8006DCF4`, 0 = unlocked, as the game
  itself writes at `0x80024F24`).

## Tested

On the developer's Windows PC (release build, 4K render, smooth outlines,
stable geometry, widescreen), attract-demo matches ran at 1.00x speed and
57-60 game FPS with the governor (stock: 20-26 FPS). Player walking speed
measured 68 units per VBlank at both 20-30 FPS and 60 FPS, the same as the
game's own step table. Linux was checked under WSL2: the launcher starts the
game, it reaches the title and plays the attract-demo match in 16:9. WSL's
OpenGL translation is too slow to judge Linux performance, which still needs a
test on a native Linux install.

## Known issues

- Pausing a match in widescreen puts the TAUNT labels back at their 4:3
  positions and darkens only the 4:3 area until you unpause
  ([#1](https://github.com/TekRantGaming/simpsons-wrestling-recompiled/issues/1)).

## Building

The repository holds the recompiled game C in `generated/`. Regenerate it only
after changing `game.toml`'s recompiler settings or the seeds:

```bash
python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "path/to/Simpsons Wrestling, The (USA).cue"
```

**Windows** (Visual Studio 2022 Build Tools, CMake, Ninja; the runtime is
built with the bundled clang-cl):

```bat
build.bat
build-launcher.bat
powershell -File packaging\package_windows.ps1
```

**Linux** (cmake, ninja, gcc, and the X11, Wayland, GL, ALSA, PulseAudio and
udev development packages; on Ubuntu: `libx11-dev libxext-dev libgl-dev
libasound2-dev libpulse-dev libwayland-dev libxkbcommon-dev libxrandr-dev
libxcursor-dev libxi-dev libxss-dev libudev-dev libgtk-3-dev zlib1g-dev`):

```bash
./build_linux.sh
./packaging/package_linux.sh
```

The launcher and the game are two programs because the psxrecomp runtime uses
SDL3 and the TRG Launcher's standalone window uses SDL2. The launcher writes
the settings and starts `SimpsonsWrestling_Recompiled` with `--no-launcher
--disc <your cue>`.

## Credits

- [psxrecomp](https://github.com/mstan/psxrecomp) and its contributors
- Cheat-code research for the NTSC-U unlock flags: the CodeBreaker code lists
  at almarsguides.com
- The Simpsons Wrestling © 2001 Twentieth Century Fox Film Corporation;
  developed by Big Ape Productions, published by Fox Interactive / Activision.
  This project is not affiliated with them and includes none of their data.
