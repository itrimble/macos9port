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

/* Writable save overlay for the virtual "OS999 HD" volume (checklist L04).
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

/* A full pathname that starts with this volume's own name ("OS999 HD:Name", the name PBGetVol reports) names the same file
 * as "Name": the prefix is matched without regard to case and removed. Nothing else with a colon is accepted. */
unsigned mac_files_volume_prefix(const char *name, size_t length) {
    static const char prefix[] = "OS999 HD:";
    const size_t n = sizeof prefix - 1;
    if (length <= n) return 0; /* the bare volume name leaves no file name */
    for (size_t i = 0; i < n; i++) {
        int a = (unsigned char)name[i], b = (unsigned char)prefix[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return (unsigned)n;
}

int mac_files_read_name(MacCpu *cpu, uint32_t pointer, char *out, size_t capacity) {
    if (!mac_files_valid(cpu, pointer, 1)) return -50;
    unsigned length = mac_cpu_read(cpu, pointer, 1);
    if (!length || length >= capacity || !mac_files_valid(cpu, pointer + 1, length)) return -50;
    uint8_t mr[256];
    memcpy(mr, mac_cpu_ram(cpu) + pointer + 1, length);
    unsigned skip = mac_files_volume_prefix((const char *)mr, length);
    /* The guest's Mac Roman bytes become precomposed UTF-8; a refused byte (':', '/', '\\', a control byte), a leading dot ("." and "..") or
     * more than 31 Mac Roman bytes is paramErr. */
    if (mr_component_to_host(mr + skip, length - skip, out, capacity) < 0) return -50;
    return 0;
}

/* A name the overlay will hold: a '/'-separated host-relative path (mac_dirs.h) whose components the guest can name: UTF-8 that maps to at most 31
 * Mac Roman bytes (macroman.h), none starting with a dot. */
int mac_files_save_name_ok(const char *name) {
    if (!mac_dirs_path_ok(name) || !name[0]) return 0;
    for (const char *p = name;;) {
        const char *slash = strchr(p, '/');
        size_t run = slash ? (size_t)(slash - p) : strlen(p);
        char part[MR_HOST_MAX];
        if (!run || run >= sizeof part) return 0;
        memcpy(part, p, run); part[run] = 0;
        if (mr_host_length(part) < 0) return 0;   /* not Mac Roman, ':' or '\\', a leading dot, or over 31 Mac Roman bytes */
        if (!slash) break;
        p = slash + 1;
    }
    return 1;
}

int mac_files_classify(const FileState *s, const char *name) {
    struct stat st;
    if (!mac_dirs_path_ok(name) || !name[0]) return FILE_NONE;
    if (s->saves >= 0 && name[0] != '.' && !mac_dirs_stat(s->saves, name, &st))
        return S_ISREG(st.st_mode) ? FILE_SAVE : FILE_SAVE_UNSUPPORTED;
    if (!mac_dirs_stat(s->directory, name, &st))
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

/* The staging copy sits beside the file: "dir/.stage-leaf". */
int mac_files_stage_name(const char *name, char *out, size_t capacity) {
    const char *slash = strrchr(name, '/');
    size_t dir = slash ? (size_t)(slash - name) + 1 : 0;
    int k = snprintf(out, capacity, "%.*s" STAGE_PREFIX "%s", (int)dir, name, name + dir);
    return k < 0 || (size_t)k >= capacity ? -1 : 0;
}

DIR *mac_files_open_listing(int directory) { return mac_dirs_opendir(directory, ""); }

/* Committed save files plus the growth of staged copies, over the whole overlay tree. Hidden entries, links and devices never count and are never
 * followed; directories are walked (bounded by ENTRY_LIMIT entries and the depth limit). */
static int usage_walk(const char *rel, int saves, unsigned *scanned, unsigned *files, int64_t *total, unsigned depth) {
    DIR *dir = mac_dirs_opendir(saves, rel);
    if (!dir) return -36;
    int error = 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) error = -36; break; }
        if (++*scanned > ENTRY_LIMIT) { error = -50; break; }
        if (e->d_name[0] == '.') continue;
        char child[DIRS_PATH_MAX];
        if (snprintf(child, sizeof child, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name) >= (int)sizeof child) continue;
        struct stat st;
        if (mac_dirs_stat(saves, child, &st)) { error = -36; break; }
        if (S_ISDIR(st.st_mode) && depth < DIRS_DEPTH_MAX) {
            if ((error = usage_walk(child, saves, scanned, files, total, depth + 1))) break;
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        (*files)++;
        *total += st.st_size;
    }
    closedir(dir);
    return error;
}
int mac_files_usage(const FileState *s, unsigned *files, uint64_t *bytes) {
    unsigned scanned = 0;
    int64_t total = 0;
    *files = 0;
    int error = usage_walk("", s->saves, &scanned, files, &total, 0);
    if (error) return error;
    for (int i = 0; i < FILE_LIMIT; i++) {
        const GuestFile *f = &s->files[i];
        struct stat staged, committed;
        if (f->fd < 0 || !f->writable || fstat(f->fd, &staged)) continue;
        int64_t base = mac_dirs_stat(s->saves, f->name, &committed) ? 0 : committed.st_size;
        total += (int64_t)staged.st_size - base;
    }
    *bytes = total < 0 ? 0 : (uint64_t)total;
    return 0;
}

static void recover(int saves, const char *rel, unsigned depth) {
    DIR *dir = mac_dirs_opendir(saves, rel);
    if (!dir) return;
    unsigned scanned = 0;
    struct dirent *e;
    while ((e = readdir(dir)) && ++scanned <= ENTRY_LIMIT) {
        struct stat st;
        char child[DIRS_PATH_MAX];
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(child, sizeof child, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name) >= (int)sizeof child) continue;
        if (mac_dirs_stat(saves, child, &st)) continue;
        if (S_ISDIR(st.st_mode) && depth < DIRS_DEPTH_MAX) { recover(saves, child, depth + 1); continue; }
        if (!strncmp(e->d_name, STAGE_PREFIX, sizeof STAGE_PREFIX - 1) && S_ISREG(st.st_mode)) mac_dirs_unlink(saves, child);
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
    recover(fd, "", 0);
    s->saves = fd;
    s->max_files = max_files;
    s->max_bytes = max_bytes;
    return 0;
}

static int open_error(void) {
    return errno == ENOENT ? -43 : errno == ELOOP || errno == EACCES || errno == EISDIR ? -54 : -36;
}

int mac_files_open_regular(int directory, const char *name, int flags) {
    int fd = mac_dirs_openat(directory, name, flags, 0);
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
        mac_files_flush_staged(s);
        int fd = mac_files_open_regular(s->saves, name, O_RDONLY);
        if (fd < 0) return fd;
        *file = (GuestFile){fd, 0, 0, {0}, 0};
        snprintf(file->name, sizeof file->name, "%s", name);
        return 0;
    }
    GuestFile *other = mac_files_open_by_name(s, name);
    if (other && other->writable) return -49; /* opWrErr: one writer per file. */
    char stage[DIRS_PATH_MAX + 16];
    if (mac_files_stage_name(name, stage, sizeof stage)) return -50;
    int src = mac_files_open_regular(s->saves, name, O_RDONLY);
    if (src < 0) return src;
    struct stat copy_size;
    if (fstat(src, &copy_size) == 0 && volume_budget_spend(&s->disk, (uint64_t)copy_size.st_size)) { close(src); return -34; }
    int dst = mac_dirs_openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (dst < 0 && errno == EEXIST && !mac_dirs_unlink(s->saves, stage))
        dst = mac_dirs_openat(s->saves, stage, O_RDWR | O_CREAT | O_EXCL, 0600);
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
    *file = (GuestFile){dst, 0, 1, {0}, 0};
    snprintf(file->name, sizeof file->name, "%s", name);
    return 0;
fail:
    close(src); close(dst); mac_dirs_unlink(s->saves, stage);
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
        char stage[DIRS_PATH_MAX + 16];
        if (mac_files_stage_name(f->name, stage, sizeof stage) || fsync(f->fd)) error = -36;
        if (!error) {
            int committed = mac_dirs_openat(s->saves, f->name, O_RDONLY, 0);
            uint8_t info[16];
            if (committed >= 0) {
                if (mac_files_finder_get(committed, info)) mac_files_finder_set(f->fd, info);
                close(committed);
            }
            if (mac_dirs_rename(s->saves, stage, f->name)) error = -36;
            else fsync(s->saves);
        }
        if (error) mac_dirs_unlink(s->saves, stage);
    }
    if (close(f->fd) && !error) error = -36;
    f->fd = -1;
    f->writable = 0;
    return error;
}

void mac_files_abort(FileState *s, GuestFile *f) {
    char stage[DIRS_PATH_MAX + 16];
    close(f->fd);
    if (f->writable && !mac_files_stage_name(f->name, stage, sizeof stage)) mac_dirs_unlink(s->saves, stage);
    f->fd = -1;
    f->writable = 0;
}

/* Save files in the overlay's top level that are not also application data (the root directory's valence, with the data files counted separately). */
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
        if (mac_dirs_stat(s->saves, e->d_name, &st)) { error = -36; break; }
        if (S_ISREG(st.st_mode) && mac_dirs_stat(s->directory, e->d_name, &st)) (*count)++;
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
    int error = mac_files_usage(s, &files, &bytes);
    if (error) return error;
    if (bytes + (uint64_t)(end - size) > s->max_bytes) return -34;
    return volume_budget_spend(&s->disk, (uint64_t)(end - size));   /* the internal disk's own capacity */
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
