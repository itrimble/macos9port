#include "system_icon_positions.h"
#include "volume_directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static const char file[] = ".os999-desktop-icons";
static int valid(const SystemIconPositions *p) {
    if (!p || p->mask < 0 || p->mask > 3) return 0;
    for (int i = 0; i < 2; i++)
        if (p->x[i] < 0 || p->x[i] > 32767 || p->y[i] < 0 || p->y[i] > 32767) return 0;
    return 1;
}
int system_icon_positions_load(const char *root, SystemIconPositions *positions) {
    if (!positions) return -1;
    memset(positions, 0, sizeof *positions);
    int dir = volume_directory_open(root, ""); if (dir < 0) return -1;
    int fd = openat(dir, file, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    int error = errno; close(dir);
    if (fd < 0) return error == ENOENT ? 0 : -1;
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
    const char header[] = "OS999-DESKTOP-ICONS-1\n";
    if (used != (size_t)st.st_size || used < sizeof header || memchr(text, 0, used) ||
        strncmp(text, header, sizeof header - 1)) return -1;
    int values[5]; char *p = text + sizeof header - 1;
    for (int i = 0; i < 5; i++) {
        char *end; errno = 0; long value = strtol(p, &end, 10);
        if (errno || end == p || value < 0 || value > (i ? 32767 : 3) || *end != (i == 4 ? '\n' : ' ')) return -1;
        values[i] = (int)value; p = end + 1;
    }
    if (*p) return -1;
    *positions = (SystemIconPositions){.mask=values[0],.x={values[1],values[3]},.y={values[2],values[4]}};
    return 1;
}
int system_icon_positions_save(const char *root, const SystemIconPositions *positions) {
    if (!valid(positions)) return -1;
    int dir = volume_directory_open(root, ""); if (dir < 0) return -1;
    char temp[96]; snprintf(temp, sizeof temp, "%s.tmp.%ld", file, (long)getpid());
    int fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) { close(dir); return -1; }
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlinkat(dir, temp, 0); close(dir); return -1; }
    int error = fprintf(f, "OS999-DESKTOP-ICONS-1\n%d %d %d %d %d\n", positions->mask,
        positions->x[0], positions->y[0], positions->x[1], positions->y[1]) < 0;
    if (fflush(f) || fsync(fd)) error = 1;
    if (fclose(f)) error = 1;
    if (!error && renameat(dir, temp, dir, file)) error = 1;
    if (error) unlinkat(dir, temp, 0);
    close(dir); return error ? -1 : 0;
}
