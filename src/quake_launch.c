#include "quake_launch.h"
#include "quake_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static int is_exec(const char *p) { struct stat st; return p[0] && !stat(p, &st) && S_ISREG(st.st_mode) && !access(p, X_OK); }
static int is_file(const char *p) { struct stat st; return !stat(p, &st) && S_ISREG(st.st_mode); }

static int fmt(char *out, size_t n, const char *a, const char *b) {
    int k = snprintf(out, n, "%s%s", a, b);
    return k > 0 && (size_t)k < n ? 0 : -1;
}

/* pak0.pk3 under data/<game>/ decides full game versus demo. */
static int find_data(QuakeLaunch *q, const char *dir) {
    char p[1100];
    if (!dir || !dir[0]) return -1;
    snprintf(p, sizeof p, "%s/baseq3/pak0.pk3", dir);
    if (is_file(p)) { snprintf(q->game, sizeof q->game, "baseq3"); return fmt(q->datapath, sizeof q->datapath, dir, ""); }
    snprintf(p, sizeof p, "%s/demota/pak0.pk3", dir);
    if (is_file(p)) { snprintf(q->game, sizeof q->game, "demota"); return fmt(q->datapath, sizeof q->datapath, dir, ""); }
    return -1;
}

int quake_launch_resolve(QuakeLaunch *q, const char *exe_dir, const char *home) {
    if (!q) return -1;
    memset(q, 0, sizeof *q);
    const char *env = getenv("OS999_QUAKE3");
    char cand[1100];
    if (env && is_exec(env)) snprintf(q->binary, sizeof q->binary, "%s", env);
    else if (exe_dir) {
        /* App bundle: Contents/MacOS/ospi -> Contents/Resources/Quake3/quake3 */
        snprintf(cand, sizeof cand, "%s/../Resources/Quake3/quake3", exe_dir);
        if (is_exec(cand)) snprintf(q->binary, sizeof q->binary, "%s", cand);
        else {
            snprintf(cand, sizeof cand, "%s/Quake3/quake3", exe_dir);
            if (is_exec(cand)) snprintf(q->binary, sizeof q->binary, "%s", cand);
        }
    }
    if (!q->binary[0]) return -1;
    snprintf(q->basepath, sizeof q->basepath, "%s", q->binary);
    char *slash = strrchr(q->basepath, '/');
    if (slash) *slash = 0; else snprintf(q->basepath, sizeof q->basepath, ".");
    const char *data = getenv("OS999_QUAKE3_DATA");
    if (data && !find_data(q, data)) return 0;
    if (home && home[0]) {
#ifdef __APPLE__
        snprintf(cand, sizeof cand, "%s/Library/Application Support/OS999/Quake3", home);
#else
        snprintf(cand, sizeof cand, "%s/.local/share/os999/quake3", home);
#endif
        if (!find_data(q, cand)) return 0;
    }
    if (!find_data(q, q->basepath)) return 0;
    return -2;
}

/* Host preflight validates the unmodified original. The separate engine also
 * authenticates the overlay and signed native modules before loading them. */
static int arena_data(QuakeLaunch *q, const char *dir) {
    char path[1100];
    if (!dir || !dir[0] || snprintf(path, sizeof path, "%s/demoq3/pak0.pk3", dir) >= (int)sizeof path) return -3;
    if (!quake_data_demo_ok(path)) return -3;
    snprintf(q->game, sizeof q->game, "demoq3");
    return fmt(q->datapath, sizeof q->datapath, dir, "");
}

int quake_launch_resolve_demo(QuakeLaunch *q, const char *exe_dir, const char *home) {
    int found = quake_launch_resolve(q, exe_dir, home);
    if (found == -1) return -1;
    q->arena_demo = 1;
    if (!home || !home[0] || fmt(q->homepath, sizeof q->homepath, home,
            "/Library/Application Support/OS999/Quake3-Arena111") != 0) return -3;
    const char *data = getenv("OS999_QUAKE3_DATA");
    if (data) return arena_data(q, data); /* Explicit bad data never falls back. */
    char path[1100];
    if (snprintf(path, sizeof path, "%s/Library/Application Support/OS999/Quake3", home) >= (int)sizeof path) return -3;
    return arena_data(q, path);
}

int quake_launch_argv(const QuakeLaunch *q, char *argv[], int max) {
    static char base_arg[1100], cd_arg[1100], home_arg[1100];
    if (!q || !argv || max < (q->arena_demo ? 23 : 11)) return 0;
    /* The engine joins argv into its own command language. Preserve spaces
     * and reject characters that could terminate a quoted path. */
    const char *paths[] = {q->basepath, q->datapath, q->homepath};
    for (int i = 0; i < 3; i++)
        if (strpbrk(paths[i], "\"\r\n")) return 0;
    snprintf(base_arg, sizeof base_arg, "\"%s\"", q->basepath);
    snprintf(cd_arg, sizeof cd_arg, "\"%s\"", q->datapath);
    snprintf(home_arg, sizeof home_arg, "\"%s\"", q->homepath);
    int n = 0;
    argv[n++] = (char *)q->binary;
    argv[n++] = "+set"; argv[n++] = "fs_basepath"; argv[n++] = base_arg;
    argv[n++] = "+set"; argv[n++] = "fs_cdpath"; argv[n++] = cd_arg;
    if (strcmp(q->game, "baseq3")) { argv[n++] = "+set"; argv[n++] = "fs_game"; argv[n++] = (char *)q->game; }
    if (q->arena_demo) {
        argv[n++] = "+set"; argv[n++] = "os999_demoProfile"; argv[n++] = "arena-1.11";
        argv[n++] = "+set"; argv[n++] = "fs_homepath"; argv[n++] = home_arg;
        argv[n++] = "+set"; argv[n++] = "sv_pure"; argv[n++] = "0";
        argv[n++] = "+set"; argv[n++] = "net_noudp"; argv[n++] = "1";
    }
    argv[n] = NULL;
    return n;
}
