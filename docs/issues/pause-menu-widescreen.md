# Pause menu in widescreen: TAUNT labels and darkening stay in the 4:3 area

**To file as a GitHub issue once the repository is published.**

## What happens

In a match with widescreen on, pausing the game (Start) shows:

- The two **TAUNT** labels jump back to their original 4:3 positions while the
  rest of the HUD (portraits, bars, button icons, win trophies) stays at the
  screen edges.
- The pause **darkening** covers only the middle 4:3 band, so the revealed
  sides of the picture stay bright.

Unpausing restores everything. Normal play, the round banner, DEMO text and the
quit dialog are not affected.

## What is known

- HUD anchoring is done in `simpsons_mods.c` at DrawOTag (`0x8005E0CC`): ordering
  table slots 2038 (panels) and 2047 (TAUNT row y=166, trophy row y=20) are
  tagged with `psx_mod_tag_hud_primitive`. Counters (`mod_counters`,
  `simpsons.hud.*`) show the hook runs and tags ~33 packets per frame while
  paused, the same as in play, and the paused frames still draw TAUNT in slot
  2047 at its usual coordinates.
- Paused frames contain no full-screen darkening primitive (checked with
  `gpu_frame_dump`); the darkening seems to come from how the paused scene is
  drawn or presented.
- The OpenGL native-wide fast path copies the canonical 4:3 centre into the
  wide surface at present time (`gl_wide_fast`, `gpu_gl_renderer.c`). A
  centre taken from a canonical image that predates the HUD shift would
  explain both symptoms. An A/B capture with `gl_wide_fast on=0` while paused
  was taken but not yet compared.

## Next steps

1. Compare paused captures with `gl_wide_fast` on and off.
2. If the fast path is the cause, find which paused frames skip the shifted
   HUD in the canonical image (the paused loop alternates full frames and
   HUD-only frames) and either re-tag those or mark the darkening as a screen
   mask (`psx_mod_tag_screen_mask_quad`) once its producer is found.
