/* android_gl_compat.c -- desktop OpenGL entry points missing on Android.
 *
 * psxrecomp's OpenGL renderer (gpu_gl_renderer.c) is desktop OpenGL 3.3 and
 * is linked into every build, but Android has no desktop OpenGL: the Android
 * build runs the software renderer (game.toml and settings.toml both say
 * renderer = "software") and SDL presents its frames with OpenGL ES. Android's
 * GLES libraries already provide every other function that file references;
 * these two only exist in desktop OpenGL. They are here so the game links,
 * and stop the game with a message if anything ever reaches them. */
#include <android/log.h>
#include <stdlib.h>

typedef unsigned int GLenum;
typedef int GLint;

static void swr_gl_missing(const char* name) {
    __android_log_print(ANDROID_LOG_FATAL, "SimpsonsWrestling",
                        "%s: desktop OpenGL is not available on Android; "
                        "set [video] renderer = \"software\"", name);
    abort();
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void* pixels) {
    (void)target; (void)level; (void)format; (void)type; (void)pixels;
    swr_gl_missing("glGetTexImage");
}

void glReadBuffer(GLenum mode) {
    (void)mode;
    swr_gl_missing("glReadBuffer");
}
