## The Simpsons Wrestling Recompiled 1.0.0 RC1

The first release candidate: The Simpsons Wrestling (PlayStation, USA) as a native Windows and Linux program. **No game files are included**; you need your own disc image of the USA release (SLUS-01227) as a `.cue` + `.bin`.

### What's in it

- **16:9 widescreen** in matches, with real extra picture and the HUD at the screen edges
- **HD rendering** up to 8K with smooth outlines and stable (wobble-free) geometry
- **60 FPS** in matches at the original game speed, with jumps, throws and walking moving exactly as far as at 30
- **120 FPS (experimental)**: an extra in-between frame drawn by the game itself; the launcher explains its limits when you pick it
- **Intro skipped** and **everything unlocked** (all wrestlers, both circuits, Bonus Match Up), both optional
- **Fast loading** for the loading screens
- **Controllers**: automatic set-up for two players, button remapping, vibration strength and stick deadzone; full keyboard remapping
- **Launcher**: monitor choice, fill-the-screen, CRT filter and scanlines, volume and sound delay, save folder shortcut

### Install

1. Unzip anywhere.
2. Run `SimpsonsWrestling.exe` (Linux: `./SimpsonsWrestling`).
3. Pick your `.cue` on the Game page and press **PLAY**.

Windows may show a SmartScreen warning because the program is not code-signed: **More info > Run anyway**.

### Known issues

- Pausing a match in widescreen puts the TAUNT labels back at their 4:3 positions and darkens only the middle until you unpause.
- 120 FPS (experimental): the ring spotlight can flicker, and on most PCs only some frames get an in-between image.
- The game's own Options menu starts with Vibration off; turn it on there.
- Linux has only been tested under WSL so far, not on a native Linux install.

### Please test

This is a release candidate. Especially useful: a circuit played through to the end with the game saving and loading your progress, two-player VS matches, and any arena or wrestler that looks or plays differently from the original. Report anything in the Issues tab with what happened, where, and your launcher settings.
