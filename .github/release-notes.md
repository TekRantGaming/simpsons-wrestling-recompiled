## The Simpsons Wrestling Recompiled v1.0.0

The Simpsons Wrestling (PlayStation, USA) as a native Windows and Linux program: the original game code translated to C and compiled for your PC, with a launcher and the options of a modern PC release.

**No game files are included.** You need your own disc image of The Simpsons Wrestling, North American release (SLUS-01227), as a `.cue` + `.bin`. The launcher asks for it the first time you open it and checks that it is the right disc; the game then reads it where it is.

### Downloads

| File | For |
| --- | --- |
| `SimpsonsWrestling-v1.0.0-windows-x64.zip` | Windows 10 and 11 (64-bit) |
| `SimpsonsWrestling-v1.0.0-linux-x86_64.AppImage` | 64-bit Linux from 2023 or later (glibc 2.38) |

### What's in it

- **16:9 widescreen** in matches, with real extra picture and the HUD at the screen edges
- **HD rendering** up to 8K, or matching your screen, with smooth outlines and stable (wobble-free) geometry
- **60 FPS** in matches at the original game speed: jumps, throws and walking move exactly as far as at 30
- **120 FPS (experimental)**: an extra in-between frame drawn by the game itself; the launcher explains its limits when you pick it
- **Intro skipped** and **everything unlocked** (all wrestlers, both circuits, Bonus Match Up), both optional
- **Fast loading** for the loading screens
- **Controllers** picked up whenever you switch them on, automatic set-up for two players, button remapping, vibration strength and stick deadzone; full keyboard remapping
- **Launcher**: monitor choice, fill-the-screen, CRT filter and scanlines, volume and sound delay, save folder shortcut

### Install

**Windows:**
1. Unzip anywhere.
2. Run `SimpsonsWrestling.exe`. The launcher opens.
3. On the **Game** page, click **Browse for the .cue file...** and pick your disc image (or drop the `.cue` onto the window).
4. Press **PLAY**.

Windows may show a SmartScreen warning because the program is not code-signed: **More info > Run anyway**.

**Linux:** make the AppImage executable (`chmod +x`, or right-click > Properties > Permissions), run it, and pick your disc the same way. The program, your settings and your saves go into a `SimpsonsWrestling-data` folder next to the AppImage.

### Known issues

- Pausing a match in widescreen puts the TAUNT labels back at their 4:3 positions and darkens only the middle until you unpause.
- 120 FPS (experimental): the ring spotlight can flicker, and on most PCs only some frames get an in-between image.
- The game's own Options menu starts with Vibration off; turn it on there.
- The Linux AppImage has been tested under WSL, not yet on a native Linux install or a Steam Deck.

Found a problem? Open an issue with what happened, where, and your launcher settings.
