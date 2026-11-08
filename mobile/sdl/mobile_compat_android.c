/* shm_open / shm_unlink for Android (see mobile_compat_android.h). */
#if defined(__ANDROID__)
#include "mobile_compat_android.h"

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int shm_path(const char *name, char *out, size_t n) {
    const char *base = SDL_AndroidGetInternalStoragePath();
    if (!base || !name || name[0] != '/' || name[1] == 0) { errno = EINVAL; return -1; }
    char dir[512];
    snprintf(dir, sizeof dir, "%s/shm", base);
    mkdir(dir, 0700);
    int w = snprintf(out, n, "%s%s", dir, name);
    if (w <= 0 || (size_t)w >= n) { errno = ENAMETOOLONG; return -1; }
    return 0;
}

int shm_open(const char *name, int oflag, mode_t mode) {
    char path[640];
    if (shm_path(name, path, sizeof path) != 0) return -1;
    return open(path, oflag | O_CLOEXEC, mode);
}

int shm_unlink(const char *name) {
    char path[640];
    if (shm_path(name, path, sizeof path) != 0) return -1;
    return unlink(path);
}
#endif
