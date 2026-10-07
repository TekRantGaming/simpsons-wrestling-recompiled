# The Simpsons Wrestling Recompiled: technical notes

How the port works, how it was measured and how to build it. Players: see the [README](../README.md).

## How the features work

All game-specific code is in [`simpsons_mods.c`](../simpsons_mods.c), a trusted
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
  overclocks the emulated CPU up to 400% (`psx_mod_set_cpu_overclock`, added
  to the framework for this port), so every frame takes one VBlank: the
  game's own timestep stays 1 and its speed is unchanged. Boot, menus and
  loading keep stock CPU timing.
- **Fighter movement at 60 FPS.** Gravity, pushes and drag change a fighter's
  velocities per unit of time (`accel * step >> 12`, step at `obj+0x14C`),
  but the position update (`0x8002E09C`) adds `(vA + vB) * 3/4` once per game
  frame. Tuned at 20-30 FPS, every velocity-driven move went about 2.5 times
  as far at 60 FPS: a jump rose ~20,900 units instead of ~8,400-9,400 and left
  the arena, and walking was about twice as fast. Instruction hooks
  (`mod_instruction_sites`) scale the velocity by `step / 170` (an average
  stock frame of 2.5 VBlanks) just before the 3/4, so distance follows time.
  Only with the 60/120 FPS option; "30 FPS (original)" is untouched.
- **120 FPS (experimental).** Game logic stays at 60 FPS; each frame gets one
  in-between image that the game draws itself, using psxrecomp's render passes
  (`docs/RENDER_PASSES.md`). At the start of frame N+1 (the task update,
  `0x8005457C`) a pass runs every task again with half the step, so the game
  works out where everything is halfway to N+1, then draws that ordering
  table into frame N's display rect with the draw environment frame N used.
  Guest time is frozen and the machine is restored afterwards, so the real
  frame runs as if nothing happened. A pass costs about as much as a game
  frame (6 ms at native resolution, much more at 4K), so passes run only while
  the game holds 60 FPS, the runtime sheds them when the PC has no time left,
  and the governor pauses them (with a growing wait) the moment the game falls
  behind, before it would ever lower the overclock.
- **Frame-rate governor.** Drawing twice the frames costs the PC twice the
  renderer time. Every quarter second the plugin compares game time with real
  time; if the game falls behind it lowers the overclock (the frame rate drops
  toward the original 20-30), and it raises it again when there is headroom.
  It ignores the first second of a match and any single slow quarter second
  (the arena loading, a host hiccup), so a hitch does not cost seconds of
  lower FPS. `SIMPSONS_GOVERNOR_LOG=<file>` logs the level, game FPS and speed
  every 2 s.
- **Fast wait for the frame flip.** After drawing a frame the game spins in
  `0x800471E0` until the VBlank handler flips the display. Overclocked, most
  of each frame went on emulating that loop. A function filter instead moves
  guest time straight to each next device event and runs the loop's own
  interrupt check until the flag clears, so the extra CPU speed costs the PC
  almost nothing when the game has finished its frame early.
- **Skip intro.** Entry hooks (`game.toml` `mod_function_entry_funcs`) end the
  copyright card's 5-second loop in the boot routine (`0x8001D0F8`) and return
  straight from PlayMovie (`0x80044628`) for the two logo movies. The memory
  card check stays, because it loads your save.
- **Unlocks.** Each VBlank the plugin sets the circuit and Bonus Match Up flags
  (`0x80072BF0`, `0x80072BF2`, `0x80072BD0`) and clears the five hidden
  wrestlers' lock words (`0x8006DCE4`..`0x8006DCF4`, 0 = unlocked, as the game
  itself writes at `0x80024F24`).

### Release-candidate additions

- **Fast loading.** psxrecomp's host-pacing accelerator (`psx_mod_set_load_acceleration`)
  speeds up the wall-clock pacing of detected CD data loads; guest time is untouched.
  This game streams from the disc in menus and matches too, so left on its own the
  accelerator ran whole matches at 1.1-1.5x with the sound muted. The plugin arms it
  (4x) only while the game's frame counter (`0x800730D0`) has not moved for 6 VBlanks,
  that is while a load holds the main loop up. psxrecomp now applies the setter live
  after activation, and a multiplier of 1 is authentic pacing (before, 1 ran every
  detected load unpaced).
- **Sound delay.** `settings.toml [audio] latency_ms` sets the output ring's fill
  target (framework default 180 ms, sized for games that pause audio production for
  ~140 ms). The launcher offers 120 / 150 / 180 ms; 120 ms measured clean in menus
  and matches (one short dip as each match starts). 80 ms ran dry after every match
  start, so it is not offered. Below 120 ms the device period drops to 512 frames.
- **Runtime settings added for the launcher:** `[audio] volume`, `[audio] latency_ms`,
  `[video] monitor` (0-based display), `[video] stretch` (fill the window) and
  `input.ini [controller] vibration` (rumble strength in percent).
- **120 FPS (experimental)** uses psxrecomp render passes at the task update
  (`0x8005457C`): each pass reruns every task with half the step, then draws the
  table into the pending frame's display rect. A pass costs about one game frame
  (~4 ms of task work plus ~1.5 ms of sandbox at native resolution, far more at 4K),
  so passes run only while the game holds 60 FPS and the governor pauses them as
  soon as the game falls behind. The ring spotlight's additive triangles are in the
  pass's table but are not drawn by the GL renderer inside passes (open).

## Measurements

On the developer's Windows PC (Ryzen 7 7800X3D, RTX 4070 Ti, 4K 120 Hz panel;
release build, 4K render, smooth outlines, stable geometry, widescreen, VSync
on), attract-demo matches in two arenas ran at 1.00x speed and a steady 60
game FPS at the 400% overclock (stock: 20-26 FPS). Emulating a match took
about 0.4 s of CPU per second of play, against 0.5 s before the PGO/LTO build
and the fast flip wait (fixed 300% overclock, VSync off). With the movement
fix, a standing jump at 60 FPS rises 8,350-8,640 units in 45-47 VBlanks (stock:
7,000-9,450 in about 44, depending on its frame mix) and walking covers
215-234 units per VBlank (stock 204-235). 120 FPS mode added an in-between
frame to 20-65% of frames at native internal resolution with the game at a
steady 60 FPS; at 4K the passes cost more than a frame has to spare, so they
mostly stay paused and the game plays at 60 FPS. Linux was checked under WSL2: the launcher starts the
game, it reaches the title and plays the attract-demo match in 16:9. WSL's
OpenGL translation is too slow to judge Linux performance, which still needs a
test on a native Linux install.

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

`build.bat` compiles with ThinLTO and profile-guided optimisation from
`pgo/windows.profdata` (about 25% less CPU per frame than a plain release
build). After large changes to the game C or the runtime, retrain that profile
with `build-pgo.bat "path\to\Simpsons Wrestling, The (USA).cue"`: it builds an
instrumented game, plays the attract demo for four minutes and rewrites the
profile.

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
