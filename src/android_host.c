/*
 * android_host.c -- Android boot glue for the NFSU2 recompilation.
 *
 * The Android counterpart of switch_nx.c. SDLActivity loads libmain.so and
 * calls SDL_main (src/main.c) on its own thread; before the game starts this
 * file sets up:
 *
 *   storage   <app external files dir>/ -- Android/data/<package>/files/ on
 *             the shared storage, reachable over USB/adb without any
 *             permission:   game/  the extracted disc (default.xbe, ...)
 *                           save/  runtime saves
 *                           nfsu2x_env.txt  KEY=VALUE per line, '#' comments
 *                           nfsu2x_log.txt  the log of the last run
 *             The process also chdir()s there: the shader/pipeline caches
 *             (progcache.bin, vkpipecache.bin, ...) use relative paths.
 *   logging   stdout and stderr go to logcat (tag "nfsu2") and the log file
 *   touch     the on-screen controls (TouchControlsView.java) push their state
 *             here through JNI; xinput_device.c reads it while no physical
 *             pad is attached to port 0
 *
 * Kept free of the runtime's headers (the Win32 vocabulary collides with
 * SDL's and Android's).
 */
#ifdef __ANDROID__
#include <SDL.h>
#include <android/log.h>
#include <jni.h>

#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG "nfsu2"
#define LOG_FILE_CAP (16u * 1024u * 1024u)

static char s_base[512];
static char s_game[600];
static char s_save[600];

const char *android_game_dir(void) { return s_game; }
const char *android_save_dir(void) { return s_save; }

/* ── Log ─────────────────────────────────────────────────────────── */

static int s_pipe[2] = { -1, -1 };
static FILE *s_logfile;

static void *log_thread(void *arg)
{
    char buf[1024], line[2048];
    size_t n = 0, written = 0;
    ssize_t r;

    (void)arg;
    while ((r = read(s_pipe[0], buf, sizeof buf)) > 0) {
        ssize_t i;
        for (i = 0; i < r; i++) {
            char c = buf[i];
            if (c != '\n' && n < sizeof line - 1)
                line[n++] = c;
            if (c == '\n' || n >= sizeof line - 1) {
                line[n] = 0;
                __android_log_write(ANDROID_LOG_INFO, TAG, line);
                if (s_logfile && written < LOG_FILE_CAP) {
                    fputs(line, s_logfile);
                    fputc('\n', s_logfile);
                    written += n + 1;
                    fflush(s_logfile);
                }
                n = 0;
            }
        }
    }
    return NULL;
}

static void log_init(void)
{
    char path[700];
    pthread_t th;

    snprintf(path, sizeof path, "%s/nfsu2x_log.txt", s_base);
    s_logfile = fopen(path, "w");
    if (pipe(s_pipe) != 0)
        return;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    dup2(s_pipe[1], STDOUT_FILENO);
    dup2(s_pipe[1], STDERR_FILENO);
    if (pthread_create(&th, NULL, log_thread, NULL) == 0)
        pthread_detach(th);
}

/* ── nfsu2x_env.txt ──────────────────────────────────────────────── */

static void load_env_file(const char *name)
{
    char path[700], line[1024];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", s_base, name);
    f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        char *eq, *end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' '))
            *--end = 0;
        if (!line[0] || line[0] == '#' || !(eq = strchr(line, '=')) || eq == line)
            continue;
        *eq = 0;
        setenv(line, eq + 1, 1);
        printf("[android] env %s=%s\n", line, eq + 1);
    }
    fclose(f);
}

/* launcher_env.txt is written by the launcher's settings screen; the person's
 * own nfsu2x_env.txt is read after it and wins. */
static void load_env(void)
{
    load_env_file("launcher_env.txt");
    load_env_file("nfsu2x_env.txt");
}

void android_boot(void)
{
    const char *ext = SDL_AndroidGetExternalStoragePath();
    if (!ext)
        ext = SDL_AndroidGetInternalStoragePath();
    snprintf(s_base, sizeof s_base, "%s", ext ? ext : "/data/local/tmp/nfsu2x");
    snprintf(s_game, sizeof s_game, "%s/game", s_base);
    snprintf(s_save, sizeof s_save, "%s/save", s_base);
    mkdir(s_base, 0777);
    mkdir(s_game, 0777);
    mkdir(s_save, 0777);
    if (chdir(s_base) != 0)
        __android_log_write(ANDROID_LOG_WARN, TAG, "chdir to the files dir failed");

    log_init();
    printf("[android] files dir %s\n", s_base);
    load_env();

    /* Landscape only; the accelerometer is not a pad. */
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_DisableScreenSaver();
    /* Back is a button of the title, not "leave the app". */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
}

/* ── On-screen controls ──────────────────────────────────────────── */

static atomic_uint s_touch_mask;       /* XInput wButtons layout, see below */
static atomic_uint s_touch_trig;       /* lt | rt << 8 */
static atomic_uint s_touch_lx, s_touch_ly, s_touch_rx, s_touch_ry;  /* int16 */
static atomic_int  s_touch_on;

/*
 * mask: 0x0001 up, 0x0002 down, 0x0004 left, 0x0008 right, 0x0010 start,
 *       0x0020 back, 0x0040 left thumb, 0x0080 right thumb, 0x0100 LB (black),
 *       0x0200 RB (white), 0x1000 A, 0x2000 B, 0x4000 X, 0x8000 Y.
 * Sticks are -32767..32767, y up.
 */
JNIEXPORT void JNICALL
Java_com_nfsu2x_NFSU2Activity_nativeTouchPad(JNIEnv *env, jclass cls, jint mask,
                                             jint lt, jint rt, jint lx, jint ly,
                                             jint rx, jint ry)
{
    (void)env; (void)cls;
    atomic_store(&s_touch_mask, (unsigned)mask);
    atomic_store(&s_touch_trig, (unsigned)((lt & 255) | ((rt & 255) << 8)));
    atomic_store(&s_touch_lx, (unsigned)(uint16_t)(int16_t)lx);
    atomic_store(&s_touch_ly, (unsigned)(uint16_t)(int16_t)ly);
    atomic_store(&s_touch_rx, (unsigned)(uint16_t)(int16_t)rx);
    atomic_store(&s_touch_ry, (unsigned)(uint16_t)(int16_t)ry);
}

/* The overlay is shown unless a physical pad is attached (Java side). */
JNIEXPORT void JNICALL
Java_com_nfsu2x_NFSU2Activity_nativeTouchEnabled(JNIEnv *env, jclass cls, jboolean on)
{
    (void)env; (void)cls;
    atomic_store(&s_touch_on, on ? 1 : 0);
    if (!on) {
        atomic_store(&s_touch_mask, 0);
        atomic_store(&s_touch_trig, 0);
    }
}

int nfsu2_android_touch_read(uint32_t *mask, uint8_t *lt, uint8_t *rt, int16_t th[4])
{
    unsigned trig;

    if (!atomic_load(&s_touch_on))
        return 0;
    trig = atomic_load(&s_touch_trig);
    *mask = atomic_load(&s_touch_mask);
    *lt = (uint8_t)(trig & 255);
    *rt = (uint8_t)((trig >> 8) & 255);
    th[0] = (int16_t)atomic_load(&s_touch_lx);
    th[1] = (int16_t)atomic_load(&s_touch_ly);
    th[2] = (int16_t)atomic_load(&s_touch_rx);
    th[3] = (int16_t)atomic_load(&s_touch_ry);
    return 1;
}
#endif /* __ANDROID__ */
