/* OS999 keeps its own working files inside the volume, in a hidden .os999
 * folder, instead of scattering them over the host Mac's home and temporary
 * folders. Nothing here is shown in Finder windows (dot-names are hidden). */
#include "ospi.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_root[OSPI_PATH];

void os999_private_set_root(const char *root) {
    snprintf(g_root, sizeof g_root, "%s", root ? root : "");
}

static int make_dir(const char *path) {
    struct stat st;
    if (mkdir(path, 0700) == 0) return 0;
    return errno == EEXIST && !lstat(path, &st) && S_ISDIR(st.st_mode) ? 0 : -1;
}

int os999_private_dir(const char *sub, char *out, size_t n) {
    if (!g_root[0] || !out || !n) return -1;
    int k = snprintf(out, n, "%s/.os999", g_root);
    if (k < 0 || (size_t)k >= n || make_dir(out)) return -1;
    if (!sub || !sub[0]) return 0;
    if (strchr(sub, '/') || !strcmp(sub, "..")) return -1;
    k = snprintf(out, n, "%s/.os999/%s", g_root, sub);
    return k < 0 || (size_t)k >= n || make_dir(out) ? -1 : 0;
}

int os999_private_temp(char *path, size_t n, const char *tag) {
    char dir[OSPI_PATH + 32];
    if (os999_private_dir("tmp", dir, sizeof dir)) {       /* no volume yet (tests, tools): host temp */
        const char *tmp = getenv("TMPDIR");
        snprintf(dir, sizeof dir, "%s", tmp && *tmp ? tmp : "/tmp");
    }
    int k = snprintf(path, n, "%s/os999-%s-XXXXXX", dir, tag ? tag : "tmp");
    return k < 0 || (size_t)k >= n ? -1 : mkstemp(path);
}
