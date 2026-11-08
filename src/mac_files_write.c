#include "mac_files_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

/* Writable save overlay for the virtual "OS Pi Data" volume (checklist L04).
 * Independent implementation of the public File Manager create/write/delete/
 * rename/file-info interface (Inside Macintosh: Files, chapter 2). Guest
 * names resolve inside one pinned save directory through openat with
 * O_NOFOLLOW; the application data directory stays read-only. Every writable
 * open works on a hidden ".stage-<name>" copy that PBClose renames over the
 * committed file, so an interrupted save never damages the previous one.
 * Stale staging files are discarded when the overlay is mounted. Quotas
 * bound the number of save files and their total bytes. */
#define STAGE_PREFIX ".stage-"
#if defined(__APPLE__)
#define FINDER_XATTR "com.apple.FinderInfo"
#define FINDER_SIZE 32
#define XATTR_GET(fd, v, n) fgetxattr(fd, FINDER_XATTR, v, n, 0, 0)
#define XATTR_SET(fd, v, n) fsetxattr(fd, FINDER_XATTR, v, n, 0, 0)
#else
#define FINDER_XATTR "user.os999.finderinfo"
#define FINDER_SIZE 16
#define XATTR_GET(fd, v, n) fgetxattr(fd, FINDER_XATTR, v, n)
#define XATTR_SET(fd, v, n) fsetxattr(fd, FINDER_XATTR, v, n, 0)
#endif
enum { ENTRY_LIMIT = 4096, LIST_LIMIT = 1024 };

int mac_files_valid(MacCpu *cpu, uint32_t address, uint32_t length) {
    size_t size = mac_cpu_ram_size(cpu);
    return address <= size && length <= size - address;
}

int mac_files_read_name(MacCpu *cpu, uint32_t pointer, char *out, size_t capacity) {
    if (!mac_files_valid(cpu, pointer, 1)) return -50;
    unsigned length = mac_cpu_read(cpu, pointer, 1);
    if (!length || length >= capacity || !mac_files_valid(cpu, pointer + 1, length)) return -50;
    memcpy(out, mac_cpu_ram(cpu) + pointer + 1, length);
    out[length] = 0;
    for (unsigned i = 0; i < length; i++)
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] >= 127 ||
            out[i] == '/' || out[i] == ':' || out[i] == '\\') return -50;
    if (!strcmp(out, ".") || !strcmp(out, "..")) return -50;
    return 0;
}

int mac_files_save_name_ok(const char *name) {
    size_t length = strlen(name);
    if (!length || length > FILE_NAME_MAX || name[0] == '.') return 0;
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] >= 127 ||
            name[i] == '/' || name[i] == ':' || name[i] == '\\') return 0;
    return 1;
}

int mac_files_classify(const FileState *s, const char *name) {
    struct stat st;
    if (s->saves >= 0 && name[0] != '.' && !fstatat(s->saves, name, &st, AT_SYMLINK_NOFOLLOW))
        return S_ISREG(st.st_mode) ? FILE_SAVE : FILE_SAVE_UNSUPPORTED;
    if (!fstatat(s->directory, name, &st, AT_SYMLINK_NOFOLLOW))
        return S_ISREG(st.st_mode) ? FILE_DATA : FILE_DATA_UNSUPPORTED;
    return FILE_NONE;
}

GuestFile *mac_files_open_by_name(FileState *s, const char *name) {
    for (int i = 0; i < FILE_LIMIT; i++)
        if (s->files[i].fd >= 0 && !strcmp(s->files[i].name, name)) return &s->files[i];
    return NULL;
}

int mac_files_volume_ok(MacCpu *cpu, uint32_t pb) {
    int volume = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    return volume == 0 || volume == -1 ? 0 : -35;
}

static int stage_name(const char *name, char *out, size_t capacity) {
    int k = snprintf(out, capacity, STAGE_PREFIX "%s", name);
    return k < 0 || (size_t)k >= capacity ? -1 : 0;
}

DIR *mac_files_open_listing(int directory) {
    int fd = openat(directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return NULL;
    DIR *dir = fdopendir(fd);
    if (!dir) close(fd);
    return dir;
}

/* Committed save files plus the growth of staged copies. Hidden entries,
 * links, directories and devices never count and are never followed. */
static int usage(const FileState *s, unsigned *files, uint64_t *bytes) {
    DIR *dir = mac_files_open_listing(s->saves);
    if (!dir) return -36;
    unsigned scanned = 0;
    int64_t total = 0;
    int error = 0;
    *files = 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) error = -36; break; }
        if (++scanned > ENTRY_LIMIT) { error = -50; break; }
        if (e->d_name[0] == '.') continue;
        struct stat st;
        if (fstatat(s->saves, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) { error = -36; break; }
        if (!S_ISREG(st.st_mode)) continue;
        (*files)++;
        total += st.st_size;
    }
    closedir(dir);
    if (error) return error;
    for (int i = 0; i < FILE_LIMIT; i++) {
        const GuestFile *f = &s->files[i];
        struct stat staged, committed;
        if (f->fd < 0 || !f->writable || fstat(f->fd, &staged)) continue;
        int64_t base = fstatat(s->saves, f->name, &committed, AT_SYMLINK_NOFOLLOW) ? 0 : committed.st_size;
        total += (int64_t)staged.st_size - base;
    }
    *bytes = total < 0 ? 0 : (uint64_t)total;
    return 0;
}

static void recover(int saves) {
    DIR *dir = mac_files_open_listing(saves);
    if (!dir) return;
    unsigned scanned = 0;
    struct dirent *e;
    while ((e = readdir(dir)) && ++scanned <= ENTRY_LIMIT) {
        struct stat st;
        if (strncmp(e->d_name, STAGE_PREFIX, sizeof STAGE_PREFIX - 1)) continue;
        if (!fstatat(saves, e->d_name, &st, AT_SYMLINK_NOFOLLOW) && S_ISREG(st.st_mode))
            unlinkat(saves, e->d_name, 0);
    }
    closedir(dir);
}

int mac_files_writable(MacRuntime *rt, const char *directory, unsigned max_files, uint64_t max_bytes) {
    FileState *s = rt ? rt->file_state : NULL;
    if (!s || s->saves >= 0 || !directory || !max_files || !max_bytes) return -1;
    if (mkdir(directory, 0700) && errno != EEXIST) return -1;
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat saves, data;
    /* The overlay must be a distinct directory: never let saves land in the
     * application data directory or anywhere the data root could alias. */
    if (fstat(fd, &saves) || fstat(s->directory, &data) ||
        (saves.st_dev == data.st_dev && saves.st_ino == data.st_ino)) { close(fd); return -1; }
    recover(fd);
    s->saves = fd;
    s->max_files = max_files;
    s->max_bytes = max_bytes;
    return 0;
}

static int open_error(void) {
    return errno == ENOENT ? -43 : errno == ELOOP || errno == EACCES || errno == EISDIR ? -54 : -36;
}

int mac_files_open_regular(int directory, const char *name, int flags) {
    int fd = openat(directory, name, flags | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return open_error();
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > INT32_MAX) { close(fd); return -54; }
    return fd;
}

int mac_files_open_save(MacRuntime *rt, const char *name, unsigned permission, GuestFile *file) {
    FileState *s = rt->file_state;
    if (!s || s->saves < 0 || !mac_files_save_name_ok(name)) return 1;
    int kind = mac_files_classify(s, name);
    if (kind == FILE_SAVE_UNSUPPORTED) return -54;
    if (kind != FILE_SAVE) return 1;
    if (permission > 4) return -50;
    if (permission == 1) {
        int fd = mac_files_open_regular(s->saves, name, O_RDONLY);
        if (fd < 0) return fd;
        *file = (GuestFile){fd, 0, 0, {0}};
        memcpy(file->name, name, strlen(name) + 1);
        return 0;
    }
    GuestFile *other = mac_files_open_by_name(s, name);
    if (other && other->writable) return -49; /* opWrErr: one writer per file. */
    char stage[64];
    if (stage_name(name, stage, sizeof stage)) return -50;
    int src = mac_files_open_regular(s->saves, name, O_RDONLY);
    if (src < 0) return src;
    int dst = openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (dst < 0 && errno == EEXIST && !unlinkat(s->saves, stage, 0))
        dst = openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (dst < 0) { close(src); return -36; }
    uint8_t buffer[16384];
    for (;;) {
        ssize_t got = read(src, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { if (got < 0) goto fail; break; }
        ssize_t done = 0;
        while (done < got) {
            ssize_t put = write(dst, buffer + done, (size_t)(got - done));
            if (put < 0 && errno == EINTR) continue;
            if (put <= 0) goto fail;
            done += put;
        }
    }
    close(src);
    *file = (GuestFile){dst, 0, 1, {0}};
    memcpy(file->name, name, strlen(name) + 1);
    return 0;
fail:
    close(src); close(dst); unlinkat(s->saves, stage, 0);
    return -36;
}

int mac_files_finder_get(int fd, uint8_t out[16]) {
    uint8_t raw[FINDER_SIZE];
    ssize_t n = XATTR_GET(fd, raw, sizeof raw);
    if (n != FINDER_SIZE) return 0;
    memcpy(out, raw, 16);
    return 1;
}

int mac_files_finder_set(int fd, const uint8_t in[16]) {
    uint8_t raw[FINDER_SIZE] = {0};
    if (FINDER_SIZE > 16) XATTR_GET(fd, raw, sizeof raw);
    memcpy(raw, in, 16);
    return XATTR_SET(fd, raw, sizeof raw) ? -36 : 0;
}

int mac_files_close(FileState *s, GuestFile *f) {
    int error = 0;
    if (f->writable) {
        char stage[64];
        if (stage_name(f->name, stage, sizeof stage) || fsync(f->fd)) error = -36;
        if (!error) {
            int committed = openat(s->saves, f->name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            uint8_t info[16];
            if (committed >= 0) {
                if (mac_files_finder_get(committed, info)) mac_files_finder_set(f->fd, info);
                close(committed);
            }
            if (renameat(s->saves, stage, s->saves, f->name)) error = -36;
            else fsync(s->saves);
        }
        if (error) unlinkat(s->saves, stage, 0);
    }
    if (close(f->fd) && !error) error = -36;
    f->fd = -1;
    f->writable = 0;
    return error;
}

void mac_files_abort(FileState *s, GuestFile *f) {
    char stage[64];
    close(f->fd);
    if (f->writable && !stage_name(f->name, stage, sizeof stage)) unlinkat(s->saves, stage, 0);
    f->fd = -1;
    f->writable = 0;
}

int mac_files_overlay_count(const FileState *s, unsigned *count) {
    *count = 0;
    if (s->saves < 0) return 0;
    DIR *dir = mac_files_open_listing(s->saves);
    if (!dir) return -36;
    unsigned scanned = 0;
    int error = 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) error = -36; break; }
        if (++scanned > ENTRY_LIMIT) { error = -50; break; }
        if (!mac_files_save_name_ok(e->d_name)) continue;
        struct stat st;
        if (fstatat(s->saves, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) { error = -36; break; }
        if (S_ISREG(st.st_mode) && fstatat(s->directory, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) (*count)++;
    }
    closedir(dir);
    return error;
}

/* Mutations on a read-only volume are vLckdErr; application data files on a
 * writable volume are fLckdErr. Links and directories are never operated on. */
int mac_files_locked_error(const FileState *s, int kind) {
    if (kind == FILE_NONE) return -43;
    if (kind == FILE_SAVE_UNSUPPORTED) return -54;
    if (kind != FILE_SAVE) return s->saves < 0 ? -46 : -45;
    return 0;
}

/* The *_name cores are shared by the PB calls (parameter-block names) and the FSSpec calls. */
int mac_files_create_name(MacRuntime *rt, const char *name) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    if (s->saves < 0) return -46;
    if (!mac_files_save_name_ok(name)) return -50;
    if (mac_files_classify(s, name) != FILE_NONE) return -48;
    unsigned files; uint64_t bytes;
    int error = usage(s, &files, &bytes);
    if (error) return error;
    if (files >= s->max_files) return -34;
    int fd = openat(s->saves, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return errno == EEXIST ? -48 : -36;
    close(fd);
    fsync(s->saves);
    return 0;
}

int mac_files_create(MacRuntime *rt, uint32_t pb) {
    char name[256];
    int error = mac_files_read_name(rt->cpu, mac_cpu_read(rt->cpu, pb + 18, 4), name, sizeof name);
    if (error) return error;
    if (!rt->file_state) return -35;
    if ((error = mac_files_volume_ok(rt->cpu, pb))) return error;
    return mac_files_create_name(rt, name);
}

int mac_files_delete_name(MacRuntime *rt, const char *name) {
    FileState *s = rt->file_state;
    int error = mac_files_locked_error(s, mac_files_classify(s, name));
    if (error) return error;
    if (mac_files_open_by_name(s, name)) return -47;
    if (unlinkat(s->saves, name, 0)) return errno == ENOENT ? -43 : -36;
    fsync(s->saves);
    return 0;
}

int mac_files_delete(MacRuntime *rt, uint32_t pb, int hfs) {
    char name[256];
    int error = mac_files_read_name(rt->cpu, mac_cpu_read(rt->cpu, pb + 18, 4), name, sizeof name);
    if (error) return error;
    if (!rt->file_state) return -35;
    if ((error = mac_files_volume_ok(rt->cpu, pb))) return error;
    if (hfs) {
        uint32_t directory = mac_cpu_read(rt->cpu, pb + 48, 4);
        if (directory != 0 && directory != 2) return -120;
    }
    return mac_files_delete_name(rt, name);
}

int mac_files_rename_names(MacRuntime *rt, const char *from, const char *to) {
    FileState *s = rt->file_state;
    int error = mac_files_locked_error(s, mac_files_classify(s, from));
    if (error) return error;
    if (!mac_files_save_name_ok(to)) return -50;
    if (mac_files_classify(s, to) != FILE_NONE) return -48;
    if (mac_files_open_by_name(s, from)) return -47;
    if (renameat(s->saves, from, s->saves, to)) return errno == ENOENT ? -43 : -36;
    fsync(s->saves);
    return 0;
}

int mac_files_rename(MacRuntime *rt, uint32_t pb) {
    char from[256], to[256];
    int error = mac_files_read_name(rt->cpu, mac_cpu_read(rt->cpu, pb + 18, 4), from, sizeof from);
    if (error) return error;
    if (!rt->file_state) return -35;
    if ((error = mac_files_volume_ok(rt->cpu, pb))) return error;
    if ((error = mac_files_read_name(rt->cpu, mac_cpu_read(rt->cpu, pb + 28, 4), to, sizeof to))) return error;
    return mac_files_rename_names(rt, from, to);
}

static int position(MacCpu *cpu, uint32_t pb, const GuestFile *f, int64_t size, int64_t *mark) {
    unsigned mode = mac_cpu_read(cpu, pb + 44, 2);
    int32_t offset = (int32_t)mac_cpu_read(cpu, pb + 46, 4);
    if (mode & ~0x33u) return -50;
    *mark = f->mark;
    switch (mode & 3u) {
    case 1: *mark = offset; break;
    case 2: *mark = size + offset; break;
    case 3: *mark += offset; break;
    }
    return *mark < 0 ? -40 : *mark > INT32_MAX ? -50 : 0;
}

/* Growth beyond the committed total must fit the byte quota before any byte
 * is written, so a rejected write leaves the staged copy unchanged. */
static int growth_allowed(FileState *s, int64_t size, int64_t end) {
    if (end <= size) return 0;
    unsigned files; uint64_t bytes;
    int error = usage(s, &files, &bytes);
    if (error) return error;
    return bytes + (uint64_t)(end - size) > s->max_bytes ? -34 : 0;
}

int mac_files_write(MacRuntime *rt, uint32_t pb, GuestFile *f) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!f->writable) return -61;
    int32_t count = (int32_t)mac_cpu_read(cpu, pb + 36, 4);
    uint32_t buffer = mac_cpu_read(cpu, pb + 32, 4);
    if (count < 0 || !mac_files_valid(cpu, buffer, (uint32_t)count)) return -50;
    struct stat st;
    if (fstat(f->fd, &st)) return -36;
    int64_t mark;
    int error = position(cpu, pb, f, st.st_size, &mark);
    if (error) return error;
    if (mark + count > INT32_MAX) return -50;
    if ((error = growth_allowed(s, st.st_size, mark + count))) return error;
    uint32_t done = 0;
    while (done < (uint32_t)count) {
        ssize_t put = pwrite(f->fd, mac_cpu_ram(cpu) + buffer + done, (size_t)count - done, mark + done);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) { error = -36; break; }
        done += (uint32_t)put;
    }
    f->mark = (int32_t)(mark + done);
    mac_cpu_write(cpu, pb + 40, 4, done);
    mac_cpu_write(cpu, pb + 46, 4, (uint32_t)f->mark);
    return error;
}

int mac_files_set_eof(MacRuntime *rt, uint32_t pb, GuestFile *f) {
    if (!f->writable) return -61;
    uint32_t eof = mac_cpu_read(rt->cpu, pb + 28, 4);
    if (eof > INT32_MAX) return -50;
    struct stat st;
    if (fstat(f->fd, &st)) return -36;
    int error = growth_allowed(rt->file_state, st.st_size, (int64_t)eof);
    if (error) return error;
    if (ftruncate(f->fd, (off_t)eof)) return -36;
    if (f->mark > (int32_t)eof) f->mark = (int32_t)eof;
    return 0;
}

int mac_files_set_fpos(MacRuntime *rt, uint32_t pb, GuestFile *f, int query) {
    MacCpu *cpu = rt->cpu;
    if (query) { mac_cpu_write(cpu, pb + 46, 4, (uint32_t)f->mark); return 0; }
    struct stat st;
    if (fstat(f->fd, &st)) return -36;
    int64_t mark;
    int error = position(cpu, pb, f, st.st_size, &mark);
    if (error) return error;
    if (mark > st.st_size) { mark = st.st_size; error = -39; }
    f->mark = (int32_t)mark;
    mac_cpu_write(cpu, pb + 46, 4, (uint32_t)mark);
    return error;
}

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
    if (stage_name(name, stage, sizeof stage)) return -50;
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
