#include "mac_files_int.h"

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Publishing the staged writes of open files. A writable open works on a hidden ".stage-<name>" copy that PBClose renames over the committed file; the
 * program, though, may look at the file before closing it (a Standard File or catalog listing, a read-only open, FlushVol) and a real Mac shows what it
 * has written. mac_files_flush_staged commits each DIRTY staging copy (same fsync, Finder-info carry-over and single rename as PBClose) and then
 * re-stages it: a fresh hidden copy of the committed file replaces the open file's descriptor, so later writes are staged again and an interruption still
 * cannot damage the committed file. Dirty means the staging copy's modification time differs from the one recorded at the last flush. Failures leave
 * the open file as it was where possible and are returned (the callers' own results do not depend on them: the listing simply shows the older data). */
static int restage(FileState *s, GuestFile *f, const char *stage) {
    int dst = openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (dst < 0 && errno == EEXIST && !unlinkat(s->saves, stage, 0))
        dst = openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (dst < 0) return -36;
    uint8_t buffer[16384]; off_t at = 0;
    for (;;) {
        ssize_t got = pread(f->fd, buffer, sizeof buffer, at);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) goto fail;
        if (got == 0) break;
        for (ssize_t done = 0; done < got;) {
            ssize_t put = write(dst, buffer + done, (size_t)(got - done));
            if (put < 0 && errno == EINTR) continue;
            if (put <= 0) goto fail;
            done += put;
        }
        at += got;
    }
    close(f->fd); f->fd = dst;
    struct stat st; f->flushed_ns = fstat(dst, &st) ? 0 : (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
    return 0;
fail:
    close(dst); unlinkat(s->saves, stage, 0);
    return -36;
}

int mac_files_flush_staged(FileState *s) {
    int error = 0;
    if (!s || s->saves < 0) return 0;
    for (unsigned i = 0; i < FILE_LIMIT; i++) {
        GuestFile *f = &s->files[i];
        struct stat st; char stage[64];
        if (f->fd < 0 || !f->writable || fstat(f->fd, &st)) continue;
        int64_t now = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
        if (now == f->flushed_ns) continue;                                    /* nothing written since the last flush */
        if (mac_files_stage_name(f->name, stage, sizeof stage) || fsync(f->fd)) { error = -36; continue; }
        int committed = openat(s->saves, f->name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        uint8_t info[16];
        if (committed >= 0) { if (mac_files_finder_get(committed, info)) mac_files_finder_set(f->fd, info); close(committed); }
        if (renameat(s->saves, stage, s->saves, f->name)) { error = -36; continue; }
        fsync(s->saves);
        if (restage(s, f, stage)) error = -36;                                 /* the data is committed; the open file now writes straight to it (no staging) until it is closed */
    }
    return error;
}
