/*
 * The Simpsons Wrestling PC features (mods/preloaded/packages/simpsons.pc).
 *
 * One package, one plugin. Each feature reads its switch from the TRG
 * Launcher's settings file (launcher.txt next to the executable), so the
 * launcher is the only place a player turns them on or off:
 *
 *   widescreen = on        16:9 presentation (GTE projection widened)
 *   frame_rate = 60        60 = real 60 FPS (faster emulated CPU), 30 = original,
 *                          120 = 60 FPS game frames plus an in-between frame each
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
/* 120 FPS render passes (see "120 FPS" below). They spend host time inside
 * the game's frame, so when the game falls behind while they run, the
 * governor pauses them (with a growing wait) instead of lowering the
 * overclock: the 60 FPS game always comes first. */
static uint32_t s_pass_rate = 0;      /* frame_rate >= 120: presentation rate */
static uint32_t s_window_passes = 0;  /* in-between frames in this window */
static double   s_pass_resume_at = 0.0;
static double   s_pass_backoff = 4.0; /* seconds; doubles after each pause */
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
    const uint32_t passes = s_window_passes;
    s_window_passes = 0;
    if (speed < kGovernorSlow && passes) {
        /* Behind while in-between frames ran: pause those at once (they are
         * optional, unlike the game's own frames). */
        s_slow_windows = 0;
        s_pass_resume_at = t + s_pass_backoff;
        if (s_pass_backoff < 64.0) s_pass_backoff *= 2.0;
        return;
    }
    if (speed >= kGovernorSlow)
        s_slow_windows = 0;
    else if (++s_slow_windows < 2u && !s_raised_last)
        return;    /* one slow window: wait for the next before acting */
    if (healthy && passes && s_pass_backoff > 4.0) s_pass_backoff *= 0.9;
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

/* 120 FPS render passes in the current log window (filled by the pass hook). */
static uint32_t s_log_pass_frames = 0, s_log_pass_images = 0;
static double   s_log_pass_seconds = 0.0;
static double   s_log_pass_task_seconds = 0.0, s_log_pass_draw_seconds = 0.0;
/* The real frame's task update, timed from its entry to the flip wait. */
static double   s_real_task_start = 0.0, s_log_real_task_seconds = 0.0;
static uint32_t s_log_real_tasks = 0;

/* SIMPSONS_PASS_TASK_TIMING (testing): host time per task handler in passes. */
static uint32_t s_task_addr[32], s_task_calls[32], s_task_count = 0;
static double   s_task_seconds[32];

static void simpsons_task_time(uint32_t handler, double seconds) {
    uint32_t i = 0;
    while (i < s_task_count && s_task_addr[i] != handler) i++;
    if (i == s_task_count) {
        if (s_task_count == 32) return;
        s_task_addr[s_task_count] = handler; s_task_calls[i] = 0; s_task_seconds[i] = 0.0;
        s_task_count++;
    }
    s_task_calls[i]++;
    s_task_seconds[i] += seconds;
}

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
    fprintf(s_gov_log, "match=%d level=%u game_fps=%.1f speed=%.3f pass_frames=%u pass_images=%u pass_ms=%.2f"
            " pass_task_ms=%.2f pass_draw_ms=%.2f real_task_ms=%.2f\n",
            simpsons_match_scene(), s_overclock_now,
            (double)(frames - s_log_frames0) / (t - s_log_t0), (double)s_log_vblanks / 59.94 / (t - s_log_t0),
            s_log_pass_frames, s_log_pass_images,
            s_log_pass_images ? 1000.0 * s_log_pass_seconds / s_log_pass_images : 0.0,
            s_log_pass_images ? 1000.0 * s_log_pass_task_seconds / s_log_pass_images : 0.0,
            s_log_pass_images ? 1000.0 * s_log_pass_draw_seconds / s_log_pass_images : 0.0,
            s_log_real_tasks ? 1000.0 * s_log_real_task_seconds / s_log_real_tasks : 0.0);
    s_log_real_task_seconds = 0.0; s_log_real_tasks = 0;
    fflush(s_gov_log);
    s_log_t0 = t; s_log_frames0 = frames; s_log_vblanks = 0;
    s_log_pass_frames = s_log_pass_images = 0; s_log_pass_seconds = 0.0;
    s_log_pass_task_seconds = s_log_pass_draw_seconds = 0.0;
    if (s_task_count) {
        fprintf(s_gov_log, "  tasks(ms/call):");
        for (uint32_t i = 0; i < s_task_count; i++)
            if (s_task_calls[i])
                fprintf(s_gov_log, " %08X=%.3f", s_task_addr[i], 1000.0 * s_task_seconds[i] / s_task_calls[i]);
        fprintf(s_gov_log, "\n");
        fflush(s_gov_log);
        s_task_count = 0;
    }
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
        s_window_passes = 0;
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
    if (s_real_task_start > 0.0) {
        s_log_real_task_seconds += now_seconds() - s_real_task_start;
        s_log_real_tasks++;
        s_real_task_start = 0.0;
    }
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

/* Fighter movement at 60 FPS. Velocities change per unit of time: gravity
 * (obj+0xA0) and the push in 0x8002D8AC add accel * step >> 12, and the drag
 * in 0x800391CC / 0x800392EC takes v * step * 8 >> 12 (step = obj+0x14C, 68
 * per VBlank). But the position update at 0x8002E09C adds (vA + vB) * 3/4
 * once per game frame, with no step. The game was tuned at its own 20-30 FPS
 * (2-3 VBlanks a frame), so at one VBlank a frame every velocity-driven move
 * (jumps, knockback, dashes, throws) covered about 2.5 times the distance: a
 * jump rose 23,100 units instead of 8,800 and left the arena. Just before each
 * axis is scaled by 3/4, the velocity sum is scaled by step / kMoveRefStep,
 * the step of an average stock-match frame (2.5 VBlanks), so distance follows
 * time instead of frames. Only with the frame-rate option on: "30 = original"
 * keeps the original behaviour. */
#define kMoveSiteX     0x8002E0A0u   /* sll v0, v1, 1   (v1 = vA.x + vB.x) */
#define kMoveSiteZ     0x8002E0BCu   /* sll v0, a1, 1   (a1 = vA.z + vB.z) */
#define kMoveSiteY     0x8002E0E4u   /* sll v1, a1, 1   (a1 = vA.y + vB.y) */
#define kFighterStep   0x14Cu
#define kMoveRefStep   170           /* 2.5 VBlanks of 68 */

static int     s_move_fix = 0;
static int32_t s_move_ref_step = kMoveRefStep;

/* SIMPSONS_TRACE_FIGHTERS=1 (testing): a mod counter per fighter object the
 * position update runs for, named by its address. */
static void simpsons_trace_fighter(uint32_t object) {
    static char     names[8][40];
    static uint32_t objects[8];
    static int      count = 0, enabled = -1;
    if (enabled < 0) enabled = getenv("SIMPSONS_TRACE_FIGHTERS") != NULL;
    if (!enabled) return;
    for (int i = 0; i < count; i++)
        if (objects[i] == object) { psx_mod_counter_add(names[i], 1); return; }
    if (count == 8) return;
    objects[count] = object;
    snprintf(names[count], sizeof names[count], "simpsons.fighter.%08X", object);
    psx_mod_counter_add(names[count++], 1);
}

static void simpsons_scale_move(struct CPUState* cpu, uint32_t address) {
    if ((address & 0x1FFFFFFFu) == (kMoveSiteX & 0x1FFFFFFFu)) simpsons_trace_fighter(cpu->gpr[4]);
    if (!s_move_fix) return;
    const uint32_t reg = (address & 0x1FFFFFFFu) == (kMoveSiteX & 0x1FFFFFFFu) ? 3u : 5u;  /* v1 : a1 */
    const int32_t step = (int32_t)psx_mod_read_word(cpu->gpr[4] + kFighterStep);  /* a0 = fighter */
    if (step <= 0 || step > 10 * 68) return;
    const int64_t v = (int32_t)cpu->gpr[reg];
    cpu->gpr[reg] = (uint32_t)(int32_t)(v * step / s_move_ref_step);
}

/* 120 FPS: in-between frames the game draws itself (psxrecomp render passes,
 * docs/RENDER_PASSES.md). Game logic stays at 60 FPS. The main loop
 * (0x80044F1C) runs every task through 0x8005457C(0, 14, step); the tasks
 * move and draw at once, filling the ordering table at gp+0x13C8. At the end
 * of a frame 0x80047158 makes that buffer current (gp+0x13CC), DrawOTags it
 * with the draw environment of the other buffer (gp+0x13D4) and arms the
 * flip, which the VBlank callback (0x80047054) performs with
 * PutDispEnv(current + 0x5C). So when the main loop starts the tasks for frame
 * N+1, frame N is drawn and waits for its flip (PENDING).
 *
 * There each render pass runs the tasks once more with a fraction of the
 * step: the game itself works out where everything is part of the way to
 * N+1 (the game is variable-timestep, and fighter movement follows the step
 * since the movement fix above). The pass then draws that table into frame
 * N's display rect with the same draw environment frame N used. The sandbox
 * freezes guest time, drops sound and CD stores and restores the machine
 * afterwards, so the real frame N+1 runs exactly as it would have. Matches
 * only; elsewhere the newest game frame is held. */
#define kGp                0x80072450u
#define kTaskUpdate        0x8005457Cu   /* tasks(mode, message, step) */
#define kTaskUpdateReturn  0x80044F44u   /* the main loop, after that call */
#define kPutDrawEnv        0x8005E13Cu
#define kGpBuildOT         (kGp + 0x13C8u)   /* table this frame's tasks fill */
#define kGpShownBuffer     (kGp + 0x13CCu)   /* buffer the pending flip shows */
#define kGpOtherBuffer     (kGp + 0x13D4u)   /* its DRAWENV draws that area */
#define kGpStep            (kGp + 0x1330u)
#define kGpFrameVBlanks    (kGp + 0x12FCu)

extern int g_psx_render_pass_active;


typedef struct { uint32_t period; } SimpsonsPassFrame;

static void simpsons_guest_call(struct CPUState* cpu, uint32_t function,
                                uint32_t a0, uint32_t a1, uint32_t a2) {
    cpu->gpr[4] = a0; cpu->gpr[5] = a1; cpu->gpr[6] = a2;
    cpu->gpr[31] = kTaskUpdateReturn;
    psx_dispatch_call(cpu, function, kTaskUpdateReturn);
}

static int simpsons_pass(struct CPUState* cpu, void* user, uint32_t alpha_q16) {
    const SimpsonsPassFrame* frame = (const SimpsonsPassFrame*)user;
    uint32_t step = (uint32_t)(((uint64_t)frame->period * 68u * alpha_q16) >> 16);
    if (step == 0) step = 1;
    psx_mod_write_word(kGpStep, step);   /* what GetStep (0x80044F74) returns */
    const double t0 = now_seconds();
    static int task_timing = -1;
    if (task_timing < 0) task_timing = getenv("SIMPSONS_PASS_TASK_TIMING") != NULL;
    if (task_timing) {
        /* Testing: the dispatcher's own loop (0x800545D8..), timing each task. */
        for (uint32_t node = psx_mod_read_word(kGp + 0xE7Cu); node; node = psx_mod_read_word(node + 4u)) {
            psx_mod_write_word(kGp + 0xE80u, node);
            const uint32_t handler = psx_mod_read_word(node + 0x10u);
            const double h0 = now_seconds();
            simpsons_guest_call(cpu, handler, 14, 0, step);
            simpsons_task_time(handler, now_seconds() - h0);
        }
        psx_mod_write_word(kGp + 0xE80u, 0);
    } else {
        simpsons_guest_call(cpu, kTaskUpdate, 0, 14, step);
    }
    const double t1 = now_seconds();
    simpsons_guest_call(cpu, kPutDrawEnv, psx_mod_read_word(kGpOtherBuffer), 0, 0);
    simpsons_guest_call(cpu, kDrawOTag, psx_mod_read_word(kGpBuildOT), 0, 0);
    s_log_pass_task_seconds += t1 - t0;
    s_log_pass_draw_seconds += now_seconds() - t1;
    return 1;
}

static void simpsons_task_update_entry(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!s_pass_rate || g_psx_render_pass_active) return;
    if (cpu->gpr[4] != 0 || cpu->gpr[5] != 14 ||
        (cpu->gpr[31] & 0x1FFFFFFFu) != (kTaskUpdateReturn & 0x1FFFFFFFu)) return;
    if (!simpsons_match_scene()) return;
    /* Only while the game itself runs at 60 FPS (one VBlank a frame), and not
     * while the governor has paused passes: a slower game never pays for
     * in-between frames, so they cannot drag it down further. */
    if (psx_mod_read_word(kGpFrameVBlanks) != 1u || now_seconds() < s_pass_resume_at) return;
    SimpsonsPassFrame user;
    user.period = 1;
    const uint32_t shown = psx_mod_read_word(kGpShownBuffer);
    PSXModRenderPassFrame frame;
    memset(&frame, 0, sizeof frame);
    frame.struct_size = sizeof frame;
    frame.period_vblanks = user.period;
    frame.shown_after_vblanks = 1;
    frame.x = psx_mod_read_half(shown + 0x5C);   /* DISPENV.disp */
    frame.y = psx_mod_read_half(shown + 0x5E);
    frame.w = psx_mod_read_half(shown + 0x60);
    frame.h = psx_mod_read_half(shown + 0x62);
    const double t0 = now_seconds();
    const uint32_t kept = psx_mod_render_pass_frame(cpu, &frame, simpsons_pass, &user);
    if (kept) s_log_pass_seconds += now_seconds() - t0;
    s_log_pass_frames++;
    s_log_pass_images += kept;
    s_window_passes += kept;
    psx_mod_counter_add("simpsons.pass.frames", 1);
    psx_mod_counter_add("simpsons.pass.images", kept);
    s_real_task_start = now_seconds();
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
        const char* ref = getenv("SIMPSONS_MOVE_REF");     /* testing: reference step */
        if (ref && atoi(ref) > 0) s_move_ref_step = atoi(ref);
        s_move_fix = !getenv("SIMPSONS_NO_MOVE_FIX");
    }
    if (atoi(s_frame_rate) >= 120 &&
        psx_mod_set_frame_interpolation_source(PSX_MOD_FRAME_SOURCE_FLIP) &&
        psx_mod_set_render_pass_flip(PSX_MOD_RENDER_PASS_FLIP_PENDING) &&
        psx_mod_set_frame_interpolation_blend(PSX_MOD_FRAME_INTERPOLATION_HOLD) &&
        psx_mod_set_frame_interpolation((uint32_t)atoi(s_frame_rate))) {
        s_pass_rate = (uint32_t)atoi(s_frame_rate);
        /* The governor pauses passes when the game falls behind, so they may
         * use all of the presenter's spare time (the runtime default is 80%). */
        if (!getenv("PSX_RENDER_PASS_BUDGET")) {
#ifdef _WIN32
            _putenv_s("PSX_RENDER_PASS_BUDGET", "100");
#else
            setenv("PSX_RENDER_PASS_BUDGET", "100", 0);
#endif
        }
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
    (void)psx_mod_register_function_entry_plugin("simpsons.pc", kTaskUpdate, simpsons_task_update_entry);
    if (!getenv("SIMPSONS_NO_FAST_WAIT"))
        (void)psx_mod_register_function_filter_plugin("simpsons.pc", kWaitForFlip, simpsons_wait_flip_filter);
    (void)psx_mod_register_instruction_plugin("simpsons.pc", kMoveSiteX, 0x00031040u, simpsons_scale_move);
    (void)psx_mod_register_instruction_plugin("simpsons.pc", kMoveSiteZ, 0x00051040u, simpsons_scale_move);
    (void)psx_mod_register_instruction_plugin("simpsons.pc", kMoveSiteY, 0x00051840u, simpsons_scale_move);
    (void)psx_mod_register_vblank_plugin("simpsons.pc", simpsons_pc_vblank);
}

