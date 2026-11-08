#include "app_switcher.h"
#include "volume_directory.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char filename[] = ".os999-application-switcher";
static int valid(const int v[8]) {
    return (v[0] == 0 || v[0] == 1) && v[1] >= 0 && v[1] <= 16384 &&
        v[2] >= TITLE_H && v[2] <= 16384 && v[3] >= 96 && v[3] <= 300 &&
        (v[4] == 0 || v[4] == 1) && (v[5] == 0 || v[5] == 1) &&
        (v[6] == 0 || v[6] == 1) && (v[7] == 0 || v[7] == 1);
}
int app_switcher_load(Session *s) {
    int dir = volume_directory_open(s->root, ""); if (dir < 0) return -1;
    int fd = openat(dir, filename, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st; char text[256];
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size >= (off_t)sizeof text) { close(fd); return -1; }
    size_t used = 0;
    while (used < sizeof text - 1) {
        ssize_t n = read(fd, text + used, sizeof text - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd); text[used] = 0;
    if (used != (size_t)st.st_size || memchr(text, 0, used) || strncmp(text, "OS999-SWITCHER-1\n", 17)) return -1;
    char *cursor = text + 17; int values[8];
    for (int i = 0; i < 8; i++) {
        char *end; errno = 0; long v = strtol(cursor, &end, 10);
        if (errno || end == cursor || v < INT_MIN || v > INT_MAX || *end != (i == 7 ? '\n' : ' ')) return -1;
        values[i] = (int)v; cursor = end + 1;
    }
    if (*cursor || !valid(values)) return -1;
    s->switcher.open = values[0]; s->switcher.x = values[1]; s->switcher.y = values[2];
    s->switcher.width = values[3]; s->switcher.small = values[4]; s->switcher.no_names = values[5];
    s->switcher.horizontal = values[6]; s->switcher.collapsed = values[6] ? 0 : values[7];
    s->switcher.gesture = s->switcher.tear_armed = 0;
    return 1;
}
int app_switcher_save(const Session *s) {
    if (!s->switcher.width) return 0; /* Never opened: leave existing preferences alone. */
    int v[8] = {s->switcher.open, s->switcher.x, s->switcher.y, s->switcher.width,
        s->switcher.small, s->switcher.no_names, s->switcher.horizontal, s->switcher.collapsed};
    if (!valid(v)) return -1;
    int dir = volume_directory_open(s->root, ""); if (dir < 0) return -1;
    char temp[96]; snprintf(temp, sizeof temp, "%s.tmp.%ld", filename, (long)getpid());
    int fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return -1; }
    FILE *f = fdopen(fd, "w"); if (!f) { close(fd); unlinkat(dir, temp, 0); close(dir); return -1; }
    int error = fprintf(f, "OS999-SWITCHER-1\n%d %d %d %d %d %d %d %d\n",
        v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]) < 0;
    if (fflush(f) || fsync(fd)) error = 1;
    if (fclose(f)) error = 1;
    if (!error && renameat(dir, temp, dir, filename)) error = 1;
    if (error) unlinkat(dir, temp, 0);
    close(dir); return error ? -1 : 0;
}
