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
#include <time.h>
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

/* A match is on screen: the top-level mode byte is 0 only in a match (and
 * during boot) and 0x800732E4 is set once the match's wrestlers exist. Only
 * matches go wide; the 2D title, menus, select and loading screens stay 4:3
 * with side bars. */
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
 * stalls at 4x).
 *
 * Drawing twice the frames costs the host twice the renderer time, and when
 * the PC cannot keep up the whole machine falls behind real time, which would
 * slow the game down. So the overclock is governed: every quarter second of
 * guest VBlanks it compares guest time with wall-clock time and steps the
 * overclock down when the game runs slow, and back up (with a growing wait
 * after each failed attempt) when there is headroom. The game then always
 * plays at its original speed, at as many frames as the PC can draw.
 *
 * One slow window is a hitch (the arena loading as a match starts, a stall in
 * the host), not a PC that is too slow: stepping down for it cost the next
 * eight seconds at 35-55 FPS while the level climbed back. So the governor
 * waits out the first second of a match and steps down only when two windows
 * in a row run slow, or straight after one of its own raises. */
#define kOverclockStep      50u
#define kGovernorWindow     15u     /* guest VBlanks per measurement (1/4 s) */
#define kGovernorSlow       0.97    /* below: step down */
#define kGovernorHealthy    0.995   /* at or above: may step up */
#define kGovernorGrace      60u     /* guest VBlanks ignored as a match starts */

static uint32_t s_overclock = 0;      /* ceiling the player asked for; 0 = off */
static int      s_governed = 1;       /* SIMPSONS_GOVERNOR=0 pins the level (testing) */
static uint32_t s_overclock_level = 0;/* governed level used in matches */
static uint32_t s_overclock_now = 100;/* what the runtime currently has */
static double   s_window_start = 0.0;
static uint32_t s_window_vblanks = 0;
static double   s_raise_after = 0.0;  /* wall time before the next step up */
static double   s_raise_backoff = 2.0;/* seconds; doubles after each failed raise */
static int      s_raised_last = 0;
static uint32_t s_slow_windows = 0;   /* slow windows in a row */
static uint32_t s_match_vblanks = 0;  /* VBlanks since the match scene began */

static double now_seconds(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static void simpsons_govern(void) {
    const double t = now_seconds();
    if (s_window_vblanks == 0) s_window_start = t;
    if (++s_window_vblanks < kGovernorWindow) return;
    const double speed = (kGovernorWindow / 59.94) / (t - s_window_start);
    s_window_vblanks = 0;
    const int healthy = speed >= kGovernorHealthy;
    if (speed >= kGovernorSlow)
        s_slow_windows = 0;
    else if (++s_slow_windows < 2u && !s_raised_last)
        return;    /* one slow window: wait for the next before acting */
    if (speed < kGovernorSlow) {
        /* Behind real time: back off, further when far behind, and wait
         * longer before trying again if a raise just caused this. */
        s_slow_windows = 0;
        const uint32_t drop = speed < 0.85 ? 2u * kOverclockStep : kOverclockStep;
        s_overclock_level = s_overclock_level > 100u + drop ? s_overclock_level - drop : 100u;
        if (s_raised_last && s_raise_backoff < 64.0) s_raise_backoff *= 2.0;
        s_raised_last = 0;
        s_raise_after = t + s_raise_backoff;
    } else if (healthy && s_raised_last && t >= s_raise_after) {
        /* The last raise held for its trial period. */
        s_raised_last = 0;
        if (s_raise_backoff > 2.0) s_raise_backoff *= 0.5;
        s_raise_after = t + s_raise_backoff;
    } else if (healthy && !s_raised_last && s_overclock_level < s_overclock && t >= s_raise_after) {
        s_overclock_level += kOverclockStep;
        s_raised_last = 1;
        s_raise_after = t + 3.0;    /* trial period */
    }
}

/* SIMPSONS_GOVERNOR_LOG=<file>: every 2 s of wall time, append the overclock
 * level, the game's own frames per second (its main loop counts frames at
 * 0x800730D0) and the game speed (guest VBlanks per wall second / 59.94). */
#define kGameFrameCounter 0x800730D0u
static FILE*    s_gov_log = NULL;
static int      s_gov_log_init = 0;
static double   s_log_t0 = 0.0;
static uint32_t s_log_frames0 = 0, s_log_vblanks = 0;

static void simpsons_governor_log(void) {
    if (!s_gov_log_init) {
        s_gov_log_init = 1;
        const char* path = getenv("SIMPSONS_GOVERNOR_LOG");
        if (path && *path) s_gov_log = fopen(path, "w");
    }
    if (!s_gov_log) return;
    const double t = now_seconds();
    const uint32_t frames = psx_mod_read_word(kGameFrameCounter);
    ++s_log_vblanks;
    if (s_log_t0 == 0.0) { s_log_t0 = t; s_log_frames0 = frames; s_log_vblanks = 0; return; }
    if (t - s_log_t0 < 2.0) return;
    fprintf(s_gov_log, "match=%d level=%u game_fps=%.1f speed=%.3f\n", simpsons_match_scene(), s_overclock_now,
            (double)(frames - s_log_frames0) / (t - s_log_t0), (double)s_log_vblanks / 59.94 / (t - s_log_t0));
    fflush(s_gov_log);
    s_log_t0 = t; s_log_frames0 = frames; s_log_vblanks = 0;
}

static void simpsons_frame_rate_vblank(void) {
    simpsons_governor_log();
    uint32_t want = 100u;
    if (s_overclock && simpsons_match_scene()) {
        if (s_match_vblanks < kGovernorGrace) ++s_match_vblanks;
        else if (s_governed) simpsons_govern();
        want = s_overclock_level;
    } else {
        s_window_vblanks = 0;
        s_slow_windows = 0;
        s_match_vblanks = 0;
    }
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

/* HUD to the screen edges. In native-wide 16:9 the game's 512-wide picture
 * sits centred, so its HUD stays where the 4:3 edges were. The game links its
 * HUD into the last slots of the frame's ordering table (DrawOTag 0x8005E0CC,
 * forward OT of 0x801 slots at buf+0x70):
 *   slot 2038: both players' panels (portrait, health and power bars, button
 *              icons). Left panel x < 237, right panel x >= 280.
 *   slot 2047: the TAUNT labels (y 166) and win trophies (y 20) near the
 *              edges, plus centred text (the "X VERSUS Y" banner, DEMO, the
 *              pause title) that must stay centred.
 * At DrawOTag every drawing command in those slots is tagged with its edge,
 * and the compositor moves it by the live reveal (nothing moves at 4:3). The
 * previous frame's tags are cleared first: packet addresses are reused. */
#define kDrawOTag         0x8005E0CCu
#define kOtSlots          0x801u
#define kSlotHudPanels    2038u
#define kSlotHudLabels    2047u
#define kMaxHudTags       256

static uint32_t s_hud_tags[kMaxHudTags];
static int      s_hud_ntags = 0;

static uint32_t gp0_command_words(uint32_t q, uint32_t packet_end) {
    const uint32_t c = psx_mod_read_word(q) >> 24;
    if (c >= 0x20 && c < 0x40) {                 /* polygon */
        const uint32_t v = (c & 0x08) ? 4u : 3u, t = (c & 0x04) ? 1u : 0u, g = (c & 0x10) ? 1u : 0u;
        return 1u + v * (1u + t) + g * (v - 1u);
    }
    if (c >= 0x40 && c < 0x60) {                 /* line; polyline ends at 0x5xxx5xxx */
        if (!(c & 0x08)) return (c & 0x10) ? 4u : 3u;
        uint32_t t = q + 8u;
        while (t < packet_end && (psx_mod_read_word(t) & 0xF000F000u) != 0x50005000u) t += 4u;
        return (t - q) / 4u + 1u;
    }
    if (c >= 0x60 && c < 0x80)                   /* rectangle */
        return 2u + ((c & 0x04) ? 1u : 0u) + (((c >> 3) & 3u) == 0u ? 1u : 0u);
    if (c >= 0xA0 && c < 0xC0) return 0u;        /* CPU->VRAM: image data follows */
    if (c >= 0x80 && c < 0xA0) return 4u;
    if (c == 0x02 || (c >= 0xC0 && c < 0xE0)) return 3u;
    return 1u;
}

static int simpsons_hud_edge(uint32_t slot, int32_t x, int32_t y) {
    if (slot == kSlotHudPanels) return x < 256 ? -1 : 1;
    const int label_row = (y >= 160 && y <= 172) || (y >= 14 && y <= 26);
    if (!label_row) return 0;
    return x < 128 ? -1 : x >= 384 ? 1 : 0;
}

static void simpsons_tag_slot(uint32_t ot, uint32_t slot) {
    const uint32_t stop = ot + 4u * (slot + 1u);
    uint32_t link = psx_mod_read_word(ot + 4u * slot) & 0xFFFFFFu;
    for (int packets = 0; link != 0xFFFFFFu && packets < 512; ++packets) {
        const uint32_t p = 0x80000000u | link;
        if (p == stop) break;
        const uint32_t hdr = psx_mod_read_word(p);
        const uint32_t end = p + 4u + 4u * (hdr >> 24);
        for (uint32_t q = p + 4u; q < end;) {
            const uint32_t w0 = psx_mod_read_word(q), op = w0 >> 24;
            const uint32_t words = gp0_command_words(q, end);
            if (!words) break;
            if (op >= 0x20 && op < 0x80) {
                const uint32_t xy = psx_mod_read_word(q + 4u);
                int32_t x = (int16_t)(xy & 0xFFFFu);
                if (op >= 0x60 && !(op & 0x04)) {    /* untextured rect: classify by centre */
                    const uint32_t size = (op >> 3) & 3u;
                    x += (size == 1u ? 1 : size == 2u ? 8 : size == 3u ? 16
                          : (int32_t)(psx_mod_read_word(q + 8u) & 0xFFFFu)) / 2;
                }
                const int edge = simpsons_hud_edge(slot, x, (int16_t)(xy >> 16));
                if (edge && s_hud_ntags < kMaxHudTags) {
                    psx_mod_tag_hud_primitive(q - 4u, edge);
                    s_hud_tags[s_hud_ntags++] = q - 4u;
                }
            }
            q += 4u * words;
        }
        link = hdr & 0xFFFFFFu;
    }
}

static void simpsons_draw_otag(struct CPUState* cpu, uint32_t address) {
    (void)address;
    for (int i = 0; i < s_hud_ntags; ++i) psx_mod_tag_hud_primitive(s_hud_tags[i], 0);
    s_hud_ntags = 0;
    psx_mod_counter_add("simpsons.hud.drawotag", 1);
    if (!is_on(s_widescreen) || !simpsons_match_scene()) {
        psx_mod_counter_add("simpsons.hud.not_match", 1);
        return;
    }
    const uint32_t ot = cpu->gpr[4];             /* a0: &ot[0] */
    if ((ot & 0xFF000000u) != 0x80000000u || (ot & 3u)) return;
    simpsons_tag_slot(ot, kSlotHudPanels);
    simpsons_tag_slot(ot, kSlotHudLabels);
    psx_mod_counter_add("simpsons.hud.tags", (uint32_t)s_hud_ntags);
}

/* Wait-for-flip fast-forward. After submitting a frame the game spins in
 * 0x800471E0 (`while (*(gp+0xCEC) == 1);`) until the VBlank callback
 * (0x80047054) flips the display and clears the flag. Overclocked, most of
 * every frame is spent emulating that loop. Instead, jump guest time straight
 * to each next observable device event and run the interrupt check the loop
 * itself would run, until the flag clears; the original body then finds it
 * clear and exits as usual. Events and interrupts happen in the same order;
 * only the spin iterations are not emulated. Bounded to three frames of guest
 * time, and only with interrupts enabled; otherwise the original loop runs. */
#define kWaitForFlip       0x800471E0u
#define kWaitForFlipCheck  0x800471F4u    /* the loop's own check point */
#define kFlipPending       0x8007313Cu    /* gp(0x80072450) + 0xCEC */
#define kWaitMaxCycles     (3u * 564480u) /* three NTSC frames */

extern uint32_t psx_idle_cycles_to_next_observable_event(void);
extern void psx_check_interrupts_at(struct CPUState* cpu, uint32_t resume_pc);

static int simpsons_wait_flip_filter(struct CPUState* cpu, uint32_t address) {
    (void)address;
    const uint32_t sr = cpu->cop0[12];
    if (!(sr & 1u) || !(sr & 0x400u)) return 0;          /* IEc / IM2 off */
    uint32_t waited = 0;
    while (psx_mod_read_word(kFlipPending) == 1u && waited < kWaitMaxCycles) {
        uint32_t step = psx_idle_cycles_to_next_observable_event();
        if (step > kWaitMaxCycles - waited) step = kWaitMaxCycles - waited;
        /* CPU charges are scaled by the overclock; ask for the scaled amount
         * so guest time moves by `step` exactly. */
        const uint64_t charge = (uint64_t)step * g_psx_cpu_overclock_pct / 100u;
        psx_advance_cycles(charge > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)charge);
        waited += step;
        psx_check_interrupts_at(cpu, kWaitForFlipCheck);
    }
    psx_mod_counter_add("simpsons.wait_flip.calls", 1);
    return 0;
}

static void simpsons_pc_activate(void) {
    load_settings();
    if (is_on(s_widescreen)) {
        (void)psx_mod_set_fixed_display_aspect(16, 9);
        psx_mod_set_world_scene_predicate(simpsons_match_scene);
    }
    if (atoi(s_frame_rate) >= 60) {
        const char* oc = getenv("SIMPSONS_OVERCLOCK");
        s_overclock = oc ? (uint32_t)atoi(oc) : 400u;
        s_overclock_level = s_overclock;
        const char* gov = getenv("SIMPSONS_GOVERNOR");
        s_governed = !(gov && gov[0] == '0');
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
    (void)psx_mod_register_function_entry_plugin("simpsons.pc", kDrawOTag, simpsons_draw_otag);
    if (!getenv("SIMPSONS_NO_FAST_WAIT"))
        (void)psx_mod_register_function_filter_plugin("simpsons.pc", kWaitForFlip, simpsons_wait_flip_filter);
    (void)psx_mod_register_vblank_plugin("simpsons.pc", simpsons_pc_vblank);
}
