/* android_chd.c -- one-time .chd -> .bin/.cue conversion for the launcher.
 *
 * A .chd disc image is compressed (LZMA, zlib, FLAC...). Playing straight
 * from it makes the game decompress the disc while it runs: on a desktop that
 * was about a quarter of the game's CPU time, which a low-end TV stick cannot
 * spare. The launcher therefore unpacks a .chd once into a plain .bin with a
 * .cue sheet (the layout the PC release uses) and plays that.
 *
 * Uses the libchdr already linked into libmain.so for the runtime's own CHD
 * support. Exposed to Java as LauncherActivity.nativeChdToBin; the launcher
 * polls nativeChdProgress for its progress bar. */
#include <libchdr/chd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CD_FRAME_SIZE 2352
#define CD_FRAMES_PER_HUNK_MAX 64

static volatile int s_progress_permille;

static void msf(char* out, size_t n, uint32_t frames) {
    snprintf(out, n, "%02u:%02u:%02u", frames / (75 * 60), (frames / 75) % 60, frames % 75);
}

/* The cue sheet track type for a CHD track type. */
static const char* cue_type(const char* type) {
    if (!strcmp(type, "MODE1_RAW") || !strcmp(type, "MODE1/2352")) return "MODE1/2352";
    if (!strcmp(type, "MODE2_RAW") || !strcmp(type, "MODE2/2352")) return "MODE2/2352";
    if (!strcmp(type, "AUDIO")) return "AUDIO";
    return NULL; /* cooked (2048/2336-byte) tracks: not used by PlayStation discs */
}

/* Converts chd_path into bin_path and writes the matching cue sheet text
 * (FILE line naming bin_name) into cue (size cue_size). Returns 0, or a
 * negative error code: -1 open, -2 not a CD image, -3 unsupported track,
 * -4 write failed, -5 read failed. */
int swr_chd_to_bin(const char* chd_path, const char* bin_path, const char* bin_name,
                   char* cue, size_t cue_size) {
    s_progress_permille = 0;
    chd_file* chd = NULL;
    if (chd_open(chd_path, CHD_OPEN_READ, NULL, &chd) != CHDERR_NONE) return -1;
    const chd_header* h = chd_get_header(chd);
    if (!h || h->unitbytes == 0 || h->hunkbytes % h->unitbytes != 0 ||
        h->unitbytes < CD_FRAME_SIZE) {
        chd_close(chd);
        return -2;
    }
    const uint32_t frames_per_hunk = h->hunkbytes / h->unitbytes;

    struct { int num; char type[32]; uint32_t frames, pregap; int pregap_in_data; } tracks[99];
    int ntracks = 0;
    uint32_t total_frames = 0;
    for (int i = 0; i < 99; i++) {
        char meta[256];
        uint32_t len = 0;
        if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, (uint32_t)i, meta, sizeof meta - 1, &len, NULL, NULL) != CHDERR_NONE)
            break;
        meta[len < sizeof meta ? len : sizeof meta - 1] = '\0';
        char type[32] = "", sub[32] = "", pgtype[32] = "", pgsub[32] = "";
        int num = 0, frames = 0, pregap = 0, postgap = 0;
        if (sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &num, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) < 4) {
            chd_close(chd);
            return -2;
        }
        if (!cue_type(type)) { chd_close(chd); return -3; }
        tracks[ntracks].num = num;
        snprintf(tracks[ntracks].type, sizeof tracks[ntracks].type, "%s", type);
        tracks[ntracks].frames = (uint32_t)frames;
        tracks[ntracks].pregap = (uint32_t)pregap;
        tracks[ntracks].pregap_in_data = pgtype[0] == 'V';
        total_frames += (uint32_t)frames;
        ntracks++;
    }
    if (ntracks == 0) { chd_close(chd); return -2; }

    FILE* out = fopen(bin_path, "wb");
    if (!out) { chd_close(chd); return -4; }
    uint8_t* hunk = (uint8_t*)malloc(h->hunkbytes);
    if (!hunk) { fclose(out); chd_close(chd); return -4; }

    size_t cue_len = (size_t)snprintf(cue, cue_size, "FILE \"%s\" BINARY\n", bin_name);
    uint32_t chd_frame = 0;   /* frame index inside the CHD (tracks are padded to 4) */
    uint32_t bin_frame = 0;   /* frame index inside the .bin */
    uint32_t cached_hunk = UINT32_MAX;
    int rc = 0;
    for (int t = 0; t < ntracks && rc == 0; t++) {
        const int audio = !strcmp(tracks[t].type, "AUDIO");
        char a[16], b[16];
        cue_len += (size_t)snprintf(cue + cue_len, cue_size > cue_len ? cue_size - cue_len : 0,
                                    "  TRACK %02d %s\n", tracks[t].num, cue_type(tracks[t].type));
        if (tracks[t].pregap && tracks[t].pregap_in_data) {
            msf(a, sizeof a, bin_frame);
            msf(b, sizeof b, bin_frame + tracks[t].pregap);
            cue_len += (size_t)snprintf(cue + cue_len, cue_size > cue_len ? cue_size - cue_len : 0,
                                        "    INDEX 00 %s\n    INDEX 01 %s\n", a, b);
        } else {
            if (tracks[t].pregap) {
                msf(a, sizeof a, tracks[t].pregap);
                cue_len += (size_t)snprintf(cue + cue_len, cue_size > cue_len ? cue_size - cue_len : 0,
                                            "    PREGAP %s\n", a);
            }
            msf(a, sizeof a, bin_frame);
            cue_len += (size_t)snprintf(cue + cue_len, cue_size > cue_len ? cue_size - cue_len : 0,
                                        "    INDEX 01 %s\n", a);
        }
        for (uint32_t f = 0; f < tracks[t].frames; f++, chd_frame++, bin_frame++) {
            const uint32_t hn = chd_frame / frames_per_hunk;
            if (hn != cached_hunk) {
                if (chd_read(chd, hn, hunk) != CHDERR_NONE) { rc = -5; break; }
                cached_hunk = hn;
            }
            uint8_t* frame = hunk + (size_t)(chd_frame % frames_per_hunk) * h->unitbytes;
            if (audio) {  /* CHD keeps CD audio big-endian */
                for (int i = 0; i < CD_FRAME_SIZE; i += 2) {
                    uint8_t x = frame[i]; frame[i] = frame[i + 1]; frame[i + 1] = x;
                }
            }
            if (fwrite(frame, 1, CD_FRAME_SIZE, out) != CD_FRAME_SIZE) { rc = -4; break; }
            if ((bin_frame & 255) == 0)
                s_progress_permille = (int)((uint64_t)bin_frame * 1000 / total_frames);
        }
        chd_frame += (4 - tracks[t].frames % 4) % 4;   /* CHD pads each track to 4 frames */
    }
    free(hunk);
    if (fclose(out) != 0 && rc == 0) rc = -4;
    chd_close(chd);
    if (rc != 0) remove(bin_path);
    else s_progress_permille = 1000;
    return rc;
}

#ifdef __ANDROID__
#include <jni.h>

JNIEXPORT jstring JNICALL
Java_io_github_ganley100_simpsonswrestling_LauncherActivity_nativeChdToBin(
        JNIEnv* env, jclass cls, jstring jchd, jstring jbin, jstring jname) {
    (void)cls;
    const char* chd = (*env)->GetStringUTFChars(env, jchd, NULL);
    const char* bin = (*env)->GetStringUTFChars(env, jbin, NULL);
    const char* name = (*env)->GetStringUTFChars(env, jname, NULL);
    char cue[8192];
    int rc = swr_chd_to_bin(chd, bin, name, cue, sizeof cue);
    (*env)->ReleaseStringUTFChars(env, jchd, chd);
    (*env)->ReleaseStringUTFChars(env, jbin, bin);
    (*env)->ReleaseStringUTFChars(env, jname, name);
    if (rc != 0) {
        char err[32];
        snprintf(err, sizeof err, "error %d", rc);
        return (*env)->NewStringUTF(env, err);
    }
    return (*env)->NewStringUTF(env, cue);
}

JNIEXPORT jint JNICALL
Java_io_github_ganley100_simpsonswrestling_LauncherActivity_nativeChdProgress(JNIEnv* env, jclass cls) {
    (void)env; (void)cls;
    return s_progress_permille;
}
#endif
