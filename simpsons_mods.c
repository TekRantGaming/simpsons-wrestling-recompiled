/*
 * The Simpsons Wrestling PC features (mods/preloaded/packages/simpsons.pc).
 *
 * One package, one plugin. Each feature reads its switch from the TRG
 * Launcher's settings file (launcher.txt next to the executable), so the
 * launcher is the only place a player turns them on or off:
 *
 *   widescreen = on        16:9 presentation (GTE projection widened)
 *   frame_rate = 60        60 = real 60 FPS (faster emulated CPU), 30 = original
 *   skip_intro = on        EA / Big Ape logos and the intro movie are skipped
 *   unlock_all = on        all wrestlers, both circuits and Bonus Match Up
 *
 * The game itself is untouched: the unlocks are the RAM flags the game keeps
 * for its own progression (the NTSC-U values the CodeBreaker codes set), written
 * once per VBlank, so beating the game still works as normal.
 */
#include "mod_plugins.h"
#include "cpu_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

/* ---- launcher.txt ------------------------------------------------------ */

static char s_widescreen[16] = "on";
static char s_frame_rate[16] = "60";
static char s_skip_intro[16] = "on";
static char s_unlock_all[16] = "on";
static int s_loaded;

static void exe_dir(char* out, size_t size) {
    out[0] = '\0';
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)size);
    if (n == 0 || n >= size) { out[0] = '\0'; return; }
#else
    ssize_t n = readlink("/proc/self/exe", out, size - 1);
    if (n <= 0) { out[0] = '\0'; return; }
    out[n] = '\0';
#endif
    char* slash = strrchr(out, '/');
#ifdef _WIN32
    char* back = strrchr(out, '\\');
    if (back > slash) slash = back;
#endif
    if (slash) slash[1] = '\0';
    else out[0] = '\0';
}

static void trim(char* s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    size_t i = 0;
    while (s[i] == ' ' || s[i] == '\t') ++i;
    if (i) memmove(s, s + i, n - i + 1);
}

static void load_settings(void) {
    if (s_loaded) return;
    s_loaded = 1;
    char path[1024];
    exe_dir(path, sizeof path - 16);
    strcat(path, "launcher.txt");
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char* hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = line;
        char* value = eq + 1;
        trim(key);
        trim(value);
        char* dst = NULL;
        if (!strcmp(key, "widescreen")) dst = s_widescreen;
        else if (!strcmp(key, "frame_rate")) dst = s_frame_rate;
        else if (!strcmp(key, "skip_intro")) dst = s_skip_intro;
        else if (!strcmp(key, "unlock_all")) dst = s_unlock_all;
        if (dst && *value) {
            strncpy(dst, value, 15);
            dst[15] = '\0';
        }
    }
    fclose(f);
}

static int is_on(const char* v) {
    return !strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "yes");
}

/* ---- features ---------------------------------------------------------- */

/* NTSC-U (SLUS-01227) progression, 16-bit flags, 1 = unlocked. */
static const uint32_t kUnlockFlags[] = {
    0x80072BF0, /* Defender circuit */
    0x80072BF2, /* Champion circuit */
    0x80072BD0, /* Bonus Match Up on the main menu */
};

/* Hidden wrestlers 8..12: one word each, nonzero = locked. The select screen
 * (0x80028E8C) re-locks a slot whose word is set; the game unlocks one by
 * storing 0 (0x80024F24). Not the 0x800741C8 table some cheat lists use: that
 * is the select screen's own "not yet taken" table, rebuilt every visit. */
#define kHiddenWrestlerLocks 0x8006DCE4u
#define kHiddenWrestlerCount 5

static void simpsons_unlock_vblank(void) {
    if (!psx_mod_game_started()) return;
    for (size_t i = 0; i < sizeof kUnlockFlags / sizeof kUnlockFlags[0]; ++i)
        if (psx_mod_read_half(kUnlockFlags[i]) == 0) psx_mod_write_half(kUnlockFlags[i], 1);
    for (uint32_t i = 0; i < kHiddenWrestlerCount; ++i)
        if (psx_mod_read_word(kHiddenWrestlerLocks + 4 * i) != 0)
            psx_mod_write_word(kHiddenWrestlerLocks + 4 * i, 0);
}

/* A match is on screen: the top-level mode byte is 0 (1 title, 2 pre-match,
 * 3 menus and loading) and the match has its two wrestlers set up. Boot also
 * has mode 0, but no wrestlers. Only matches go wide; the 2D title, menus,
 * select screens and loading screens stay 4:3 with side bars. */
#define kGameMode      0x8007398Cu
#define kMatchWrestlers 0x800732E4u

static int simpsons_match_scene(void) {
    return psx_mod_read_byte(kGameMode) == 0 && psx_mod_read_byte(kMatchWrestlers) != 0;
}

/* 60 FPS. The game is variable-timestep: each frame it counts the VBlanks
 * since the last one (0x80044F80) and scales movement by a step from a linear
 * table (68 per VBlank). A match only runs at 20-30 FPS because a frame costs
 * more than one VBlank of R3000A time, so with a faster CPU every frame takes
 * one VBlank: real 60 FPS frames at the original game speed. Only matches are
 * overclocked; boot, menus and loading keep stock timing (some boot code
 * stalls at 4x). 300% keeps every measured match frame at one VBlank. */
static uint32_t s_overclock = 0;
static uint32_t s_overclock_now = 100;

static void simpsons_frame_rate_vblank(void) {
    const uint32_t want = (s_overclock && simpsons_match_scene()) ? s_overclock : 100u;
    if (want != s_overclock_now && psx_mod_set_cpu_overclock(want)) s_overclock_now = want;
}

/* Skip Intro. The boot routine (0x8001D0F8) shows the copyright card until
 * its frame steps add up to 0x5000 (about 5 s), plays FOXLOGO.STR and
 * BIGAPE.STR through PlayMovie (0x80044628, only called from there), then
 * checks the memory card (which loads the save, so that part stays). */
#define kPlayMovie        0x80044628u
#define kFrameStep        0x80044F74u
#define kCopyrightLoopRa  0x8001D158u
#define kCopyrightLength  0x5000u

static int simpsons_no_logo_movies(struct CPUState* cpu, uint32_t address) {
    (void)address;
    cpu->gpr[2] = 0;
    return 1;
}

static int simpsons_short_copyright(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (cpu->gpr[31] != kCopyrightLoopRa) return 0;
    cpu->gpr[2] = kCopyrightLength;
    return 1;
}

static int simpsons_skip_movies_filter(struct CPUState* cpu, uint32_t address) {
    load_settings();
    return is_on(s_skip_intro) ? simpsons_no_logo_movies(cpu, address) : 0;
}

static int simpsons_copyright_filter(struct CPUState* cpu, uint32_t address) {
    load_settings();
    return is_on(s_skip_intro) ? simpsons_short_copyright(cpu, address) : 0;
}

static void simpsons_pc_activate(void) {
    load_settings();
    if (is_on(s_widescreen)) {
        (void)psx_mod_set_fixed_display_aspect(16, 9);
        psx_mod_set_world_scene_predicate(simpsons_match_scene);
    }
    if (atoi(s_frame_rate) >= 60) {
        const char* oc = getenv("SIMPSONS_OVERCLOCK");
        s_overclock = oc ? (uint32_t)atoi(oc) : 300u;
    }
    if (is_on(s_skip_intro)) (void)psx_mod_set_auto_skip_fmv(1);
}

static void simpsons_pc_vblank(void) {
    load_settings();
    if (is_on(s_unlock_all)) simpsons_unlock_vblank();
    simpsons_frame_rate_vblank();
}

PSX_MOD_CONSTRUCTOR(simpsons_register_mod_plugins) {
    (void)psx_mod_register_activation_plugin("simpsons.pc", simpsons_pc_activate);
    /* Registered once; they run only while the package's plugin is active.
     * The settings file decides whether they change anything. */
    (void)psx_mod_register_function_filter_plugin("simpsons.pc", kPlayMovie, simpsons_skip_movies_filter);
    (void)psx_mod_register_function_filter_plugin("simpsons.pc", kFrameStep, simpsons_copyright_filter);
    (void)psx_mod_register_vblank_plugin("simpsons.pc", simpsons_pc_vblank);
}
