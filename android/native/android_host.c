/* android_host.c -- Android start-up for the recompiled game.
 *
 * The runtime keeps its settings (settings.toml, input.ini, keybinds.ini,
 * launcher.txt), its data (game.toml, mods/bundled, the BIOS) and the memory
 * cards next to the game program, and finds that folder from argv[0]. On
 * Android argv[0] is "app_process" and the working directory is "/", so the
 * app (GameActivity.java) sets SWR_HOME to its own files folder before it
 * loads libmain.so, and this constructor moves there before main() runs.
 *
 * SWR_AndroidMain is the entry point SDL calls (GameActivity.getMainFunction):
 * SDL passes argv[0] = "app_process", which the runtime would read as a folder
 * name for keybinds.ini, so it hands the game an argv[0] that names a file in
 * that folder instead, then calls the game's own main() (SDL_main).
 *
 * It also forwards stdout/stderr to logcat (tag "SimpsonsWrestling"), where
 * Android otherwise throws them away, and to last_run.log in the game folder,
 * which the launcher can show on the TV. */
#ifdef __ANDROID__
#include <android/log.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int s_pipe[2];
static int s_logfile = -1;   /* last_run.log in the game folder */

static void* log_pump(void* arg) {
    (void)arg;
    char buf[1024];
    size_t len = 0;
    for (;;) {
        ssize_t n = read(s_pipe[0], buf + len, sizeof buf - 1 - len);
        if (n <= 0) break;
        if (s_logfile >= 0) (void)!write(s_logfile, buf + len, (size_t)n);
        len += (size_t)n;
        buf[len] = '\0';
        char* start = buf;
        char* nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, "SimpsonsWrestling", start);
            start = nl + 1;
        }
        len = strlen(start);
        if (len == sizeof buf - 1) {  /* a line longer than the buffer */
            __android_log_write(ANDROID_LOG_INFO, "SimpsonsWrestling", start);
            len = 0;
        } else {
            memmove(buf, start, len + 1);
        }
    }
    return NULL;
}

__attribute__((constructor)) static void swr_android_start(void) {
    const char* home = getenv("SWR_HOME");
    if (home && *home && chdir(home) != 0)
        __android_log_print(ANDROID_LOG_ERROR, "SimpsonsWrestling", "chdir(%s) failed", home);

    /* The launcher's "Show last game log" reads this. */
    s_logfile = open("last_run.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pipe(s_pipe) == 0) {
        dup2(s_pipe[1], STDOUT_FILENO);
        dup2(s_pipe[1], STDERR_FILENO);
        pthread_t t;
        if (pthread_create(&t, NULL, log_pump, NULL) == 0) pthread_detach(t);
    }
}

int SDL_main(int argc, char* argv[]);

__attribute__((visibility("default"))) int SWR_AndroidMain(int argc, char* argv[]) {
    static char exe[4096];
    if (argc > 0 && getcwd(exe, sizeof exe - 32)) {
        strcat(exe, "/SimpsonsWrestling.so");
        argv[0] = exe;
    }
    return SDL_main(argc, argv);
}
#endif
