#include "mac_files_int.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Hidden runtime files (".resedits-<16 hex>") in the save overlay: the guest cannot see, open or replace
 * them because dot-names are excluded from every guest-facing lookup. Same staged-copy-then-rename commit
 * as guest saves, and a size cap of a quarter of the overlay byte quota. */
static int hidden_name_ok(const char *name) {
    static const char prefix[] = ".resedits-";
    size_t n = strlen(name);
    if (n != sizeof prefix - 1 + 16 || memcmp(name, prefix, sizeof prefix - 1)) return 0;
    for (size_t i = sizeof prefix - 1; i < n; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f'))) return 0;
    return 1;
}

int mac_files_overlay_put_hidden(MacRuntime *rt, const char *name, const uint8_t *data, size_t size) {
    FileState *s = rt ? rt->file_state : NULL;
    if (!s || s->saves < 0) return 1;
    if (!hidden_name_ok(name)) return -50;
    if (size > s->max_bytes / 4) return -34;
    char stage[64];
    if (mac_files_stage_name(name, stage, sizeof stage)) return -50;
    int fd = openat(s->saves, stage, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0 && errno == EEXIST && !unlinkat(s->saves, stage, 0))
        fd = openat(s->saves, stage, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -36;
    size_t done = 0;
    while (done < size) {
        ssize_t put = write(fd, data + done, size - done);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) { close(fd); unlinkat(s->saves, stage, 0); return -36; }
        done += (size_t)put;
    }
    if (fsync(fd) || close(fd) || renameat(s->saves, stage, s->saves, name)) { unlinkat(s->saves, stage, 0); return -36; }
    fsync(s->saves);
    return 0;
}

int mac_files_overlay_get_hidden(MacRuntime *rt, const char *name, size_t limit, uint8_t **data, size_t *size) {
    FileState *s = rt ? rt->file_state : NULL;
    if (!s || s->saves < 0 || !hidden_name_ok(name)) return 1;
    int fd = mac_files_open_regular(s->saves, name, O_RDONLY);
    if (fd < 0) return 1;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 0 || (uint64_t)st.st_size > limit) { close(fd); return 1; }
    uint8_t *buf = malloc(st.st_size ? (size_t)st.st_size : 1);
    size_t got = 0;
    while (buf && got < (size_t)st.st_size) {
        ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd);
    if (!buf || got != (size_t)st.st_size) { free(buf); return 1; }
    *data = buf; *size = got;
    return 0;
}
