#include "finder_icon_position.h"
#include "volume_directory.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/xattr.h>

static const char attribute[] = "user.os999.desktop-position";
static int item_open(const char *root, const char *rel, const char *name) {
    if (!name || !name[0] || strlen(name) >= OSPI_NAME || name[0] == '.' ||
        strchr(name, '/') || strchr(name, '\n') || strchr(name, '\r')) { errno = EINVAL; return -1; }
    int dir = volume_directory_open(root, rel);
    if (dir < 0) return -1;
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    int error = errno; close(dir);
    if (fd < 0) { errno = error; return -1; }
    struct stat st;
    if (fstat(fd, &st) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
        close(fd); errno = EINVAL; return -1;
    }
    return fd;
}
int finder_icon_position_read(const char *root, const char *rel, const char *name, int *x, int *y) {
    if (!x || !y) return -1;
    int fd = item_open(root, rel, name); if (fd < 0) return -1;
    char text[64];
#ifdef __APPLE__
    ssize_t n = fgetxattr(fd, attribute, text, sizeof text - 1, 0, 0);
#else
    ssize_t n = fgetxattr(fd, attribute, text, sizeof text - 1);
#endif
    int error = errno; close(fd);
    if (n < 0) {
#ifdef __APPLE__
        if (error == ENOATTR) return 0;
#else
        if (error == ENODATA) return 0;
#endif
        return -1;
    }
    const char prefix[] = "OS999-ICON-1 ";
    if (n <= (ssize_t)sizeof prefix || memchr(text, 0, (size_t)n) ||
        strncmp(text, prefix, sizeof prefix - 1)) return -1;
    text[n] = 0;
    char *p = text + sizeof prefix - 1, *end;
    errno = 0; long a = strtol(p, &end, 10);
    if (errno || end == p || *end != ' ' || a < 0 || a > 32767) return -1;
    p = end + 1; errno = 0; long b = strtol(p, &end, 10);
    if (errno || end == p || *end != '\n' || end[1] || b < 0 || b > 32767) return -1;
    *x = (int)a; *y = (int)b; return 1;
}
int finder_icon_position_write(const char *root, const char *rel, const char *name, int x, int y) {
    if (x < 0 || x > 32767 || y < 0 || y > 32767) return -1;
    int fd = item_open(root, rel, name); if (fd < 0) return -1;
    char text[64]; int n = snprintf(text, sizeof text, "OS999-ICON-1 %d %d\n", x, y);
#ifdef __APPLE__
    int result = fsetxattr(fd, attribute, text, (size_t)n, 0, 0);
#else
    int result = fsetxattr(fd, attribute, text, (size_t)n, 0);
#endif
    int error = errno; close(fd); errno = error; return result;
}
