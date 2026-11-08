/* Platform plumbing for the SDL host: resources, volume, haptics and the
 * Android JNI entry points that feed Jetpack WindowManager fold reports into
 * the bridge. */
#include "mobile_platform.h"
#include "os999_mobile.h"
#include "ospi.h"
#include "desktop_host.h"

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__ANDROID__)
#include <jni.h>

/* ---- unpack the APK's assets once per build ---- */

static int copy_asset(const char *rel, const char *dest_root) {
    char dest[1024];
    snprintf(dest, sizeof dest, "%s/%s", dest_root, rel);
    for (char *p = dest + strlen(dest_root) + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(dest, 0755); *p = '/'; }
    SDL_RWops *in = SDL_RWFromFile(rel, "rb");
    if (!in) return -1;
    FILE *out = fopen(dest, "wb");
    if (!out) { SDL_RWclose(in); return -1; }
    char buf[65536];
    size_t n;
    while ((n = SDL_RWread(in, buf, 1, sizeof buf)) > 0) fwrite(buf, 1, n, out);
    SDL_RWclose(in);
    return fclose(out) == 0 ? 0 : -1;
}

static int unpack_assets(char *root, size_t n) {
    const char *base = SDL_AndroidGetInternalStoragePath();
    if (!base) return -1;
    snprintf(root, n, "%s/res", base);
    mkdir(root, 0755);
    size_t list_n = 0;
    char *list = SDL_LoadFile("os999-assets.txt", &list_n);
    if (!list) return -1;
    char stamp_path[1024];
    snprintf(stamp_path, sizeof stamp_path, "%s/.unpacked", root);
    size_t old_n = 0;
    char *old = SDL_LoadFile(stamp_path, &old_n);
    int fresh = old && old_n == list_n && memcmp(old, list, list_n) == 0;
    SDL_free(old);
    if (!fresh) {
        char *save = NULL;
        for (char *line = strtok_r(list, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
            if (*line && copy_asset(line, root) != 0)
                SDL_Log("OS999: could not unpack %s", line);
        SDL_free(list);
        list = SDL_LoadFile("os999-assets.txt", &list_n);
        FILE *f = list ? fopen(stamp_path, "wb") : NULL;
        if (f) { fwrite(list, 1, list_n, f); fclose(f); }
    }
    SDL_free(list);
    return 0;
}

static void android_haptic(int type, float strength, float sharpness, int duration_ms) {
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID m = (*env)->GetMethodID(env, cls, "performOs999Haptic", "(IFFI)V");
    if (m) (*env)->CallVoidMethod(env, activity, m, type, strength, sharpness, duration_ms);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
}

#define FOLD_JNI(name) JNIEXPORT void JNICALL Java_org_remnantsecurity_os999_FoldBridge_##name

FOLD_JNI(nativeReportFold)(JNIEnv *e, jclass c, jint state, jint axis, jint l, jint t, jint r, jint b) {
    (void)e; (void)c;
    os999_mobile_report_fold(state, axis, l, t, r, b);
}
FOLD_JNI(nativeReportDisplay)(JNIEnv *e, jclass c, jint inner, jfloat ppi) {
    (void)e; (void)c;
    os999_mobile_report_display(inner, ppi);
}
FOLD_JNI(nativeReportInsets)(JNIEnv *e, jclass c, jint l, jint t, jint r, jint b) {
    (void)e; (void)c;
    os999_mobile_report_insets(l, t, r, b);
}
FOLD_JNI(nativeReportTablet)(JNIEnv *e, jclass c, jboolean tablet) {
    (void)e; (void)c;
    os999_mobile_report_tablet(tablet ? 1 : 0);
}
FOLD_JNI(nativeReportHardwareKeyboard)(JNIEnv *e, jclass c, jboolean attached) {
    (void)e; (void)c;
    os999_mobile_report_hw_keyboard(attached ? 1 : 0);
}
FOLD_JNI(nativeReportMagnify)(JNIEnv *e, jclass c, jfloat factor) {
    (void)e; (void)c;
    os999_mobile_report_magnify(factor);
}
FOLD_JNI(nativeReportScroll)(JNIEnv *e, jclass c, jfloat lines) {
    (void)e; (void)c;
    os999_mobile_report_scroll(lines);
}
#endif

static const char *flag(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++)
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    return NULL;
}

int mobile_platform_resources(int argc, char **argv) {
#if defined(__ANDROID__)
    (void)argc; (void)argv;
    char root[1024];
    if (unpack_assets(root, sizeof root) != 0) return -1;
    return chdir(root);
#elif MOBILE_PHONE
    (void)argc; (void)argv;
    char *base = SDL_GetBasePath();   /* the .app bundle */
    int rc = base ? chdir(base) : -1;
    SDL_free(base);
    return rc;
#else
    const char *dir = flag(argc, argv, "--resources");
    return dir ? chdir(dir) : 0;   /* default: run from the repository root */
#endif
}

int mobile_platform_volume(int argc, char **argv, char *out, size_t n) {
#if MOBILE_PHONE
    (void)argc; (void)argv;
    char *pref = SDL_GetPrefPath("Remnant Security", "OS999");
    if (!pref) return -1;
    char path[OSPI_PATH];
    snprintf(path, sizeof path, "%sVolume", pref);
    SDL_free(pref);
    if (volume_mkdir_p(path) != 0 || volume_seed(path) != 0) return -1;
    char resolved[PATH_MAX];
    if (!realpath(path, resolved) || strlen(resolved) >= n) return -1;
    snprintf(out, n, "%s", resolved);
    return 0;
#else
    return desktop_host_resolve_volume(argc, argv, out, n);
#endif
}

void mobile_platform_start(void) {
#if defined(__ANDROID__)
    os999_mobile_set_haptics(android_haptic);
#elif MOBILE_PHONE
    mobile_ios_start();
#endif
}

void mobile_platform_poll(void) {
#if defined(__APPLE__) && MOBILE_PHONE
    mobile_ios_poll();
#endif
}
