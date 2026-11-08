#ifndef OSPI_VOLUME_DIRECTORY_INCLUDED
#define OSPI_VOLUME_DIRECTORY_INCLUDED
#include "ospi.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* Return an owned descriptor; no component below the volume may be a link. */
static inline int volume_directory_open(const char *root, const char *rel) {
    if (!root || !rel || strlen(rel) >= OSPI_PATH || rel[0] == '/') {
        errno = EINVAL; return -1;
    }
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0 || !rel[0]) return fd;
    char path[OSPI_PATH]; strcpy(path, rel);
    char *part = path;
    while (part) {
        char *slash = strchr(part, '/');
        if (slash) *slash = 0;
        if (!part[0] || part[0] == '.' || strlen(part) >= OSPI_NAME ||
            strchr(part, '\n') || strchr(part, '\r')) {
            close(fd); errno = EINVAL; return -1;
        }
        int child = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        int error = errno;
        close(fd);
        if (child < 0) { errno = error; return -1; }
        fd = child; part = slash ? slash + 1 : NULL;
    }
    return fd;
}

int volume_disk_cache_lookup(const char *full, size_t req_n, char *buf);
void volume_disk_cache_store(const char *full, size_t req_n, const char *buf);
#endif
