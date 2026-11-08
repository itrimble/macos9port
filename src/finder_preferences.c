#include "finder_preferences.h"
#include "volume_directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char file[] = ".os999-finder-preferences";
static int valid(long enabled, long delay) {
    return (enabled == 0 || enabled == 1) && delay >= 100 && delay <= 2000;
}
int finder_preferences_load(Session *s) {
    s->spring_disabled = 0; s->spring_delay_ms = 600;
    int dir = volume_directory_open(s->root, ""); if (dir < 0) return -1;
    int fd = openat(dir, file, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); close(dir);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    struct stat st; char text[128]; size_t used = 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 1 || st.st_size >= (off_t)sizeof text) { close(fd); return -1; }
    while (used < sizeof text - 1) {
        ssize_t n = read(fd, text + used, sizeof text - 1 - used);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        if (!n) break;
        used += (size_t)n;
    }
    close(fd); text[used] = 0;
    const char header[] = "OS999-PREFERENCES-1\n";
    if (used != (size_t)st.st_size || memchr(text, 0, used) || strncmp(text, header, sizeof header - 1)) return -1;
    char *end, *p = text + sizeof header - 1; errno = 0;
    long enabled = strtol(p, &end, 10); if (errno || end == p || *end != ' ') return -1;
    p = end + 1; errno = 0; long delay = strtol(p, &end, 10);
    if (errno || end == p || *end != '\n' || end[1] || !valid(enabled, delay)) return -1;
    s->spring_disabled = !enabled; s->spring_delay_ms = (int)delay; return 1;
}
int finder_preferences_save(Session *s, int enabled, int delay) {
    if (!valid(enabled, delay)) return -1;
    int dir = volume_directory_open(s->root, ""); if (dir < 0) return -1;
    char temp[96]; snprintf(temp, sizeof temp, "%s.tmp.%ld", file, (long)getpid());
    int fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return -1; }
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlinkat(dir, temp, 0); close(dir); return -1; }
    int error = fprintf(f, "OS999-PREFERENCES-1\n%d %d\n", enabled, delay) < 0;
    if (fflush(f) || fsync(fd)) error = 1;
    if (fclose(f)) error = 1;
    if (!error && renameat(dir, temp, dir, file)) error = 1;
    if (error) unlinkat(dir, temp, 0);
    close(dir);
    if (!error) { s->spring_disabled = !enabled; s->spring_delay_ms = delay; }
    return error ? -1 : 0;
}
