#include "mac_runtime.h"
#include "mac_files_int.h"
#include "mac_drive_queue.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Independent synchronous PBOpen/PBRead/PBClose/PBGetVol/GetWDInfo implementation from the public
 * File Manager interface: Inside Macintosh: Files, chapter 2, pp. 2-122, 2-292.
 * https://leopard-adc.pepas.com/documentation/mac/pdf/Files/File_Manager.pdf
 * Only ASCII sibling data files on the default volume, opened read-only.
 * Writes, creates, deletes, renames and Finder info go to the optional save
 * overlay in mac_files_write.c (mac_files_writable). No drivers, resource-fork
 * opens, async callbacks or HFS paths.
 */

/* Borrowed pinned root; resource loader must use openat and never close this fd. */
int mac_files_directory_fd(const MacRuntime *rt) {
    const FileState *state = rt ? rt->file_state : NULL;
    return state ? state->directory : -1;
}

static int valid(MacCpu *cpu, uint32_t address, uint32_t length) {
    return mac_files_valid(cpu, address, length);
}

/* Open staged writes are discarded, never committed: a halt mid-save leaves
 * the previously committed save intact (restart recovery). */
void mac_files_destroy(MacRuntime *rt) {
    if (!rt || !rt->file_state) return;
    FileState *s = rt->file_state;
    for (int i = 0; i < FILE_LIMIT; i++) if (s->files[i].fd >= 0) mac_files_abort(s, &s->files[i]);
    close(s->directory);
    if (s->saves >= 0) close(s->saves);
    free(s);
    rt->file_state = NULL;
    mac_drive_unmount(rt, 1);
}

int mac_files_root(MacRuntime *rt, const char *directory) {
    char path[PATH_MAX];
    if (!rt || !directory || rt->file_state || !realpath(directory, path)) return -1;
    int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    FileState *s = calloc(1, sizeof *s);
    if (!s) { close(fd); return -1; }
    uint32_t blocks;
    if (mac_volume_blocks(fd, &blocks) || mac_drive_mount(rt, 1, blocks)) {
        close(fd); free(s); return -1;
    }
    s->directory = fd;
    s->saves = -1;
    for (int i = 0; i < FILE_LIMIT; i++) s->files[i].fd = -1;
    rt->file_state = s;
    return 0;
}

static int result(MacCpu *cpu, uint32_t pb, int error) {
    mac_cpu_write(cpu, pb + 16, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (error < 0 ? 8u : error == 0 ? 4u : 0u));
    return 1;
}

/* This virtual volume is precisely the explicitly mounted app-data directory,
 * not a host disk. Its stable reference never grants access to other paths. */
int mac_files_get_volume(MacRuntime *rt, uint32_t pb) {
    static const char name[] = "OS Pi Data";
    if (!rt->file_state) return -35;
    uint32_t p = mac_cpu_read(rt->cpu, pb + 18, 4);
    if (p && !valid(rt->cpu, p, sizeof name)) return -50;
    if (p) {
        mac_cpu_ram(rt->cpu)[p] = sizeof name - 1;
        memcpy(mac_cpu_ram(rt->cpu) + p + 1, name, sizeof name - 1);
    }
    mac_cpu_write(rt->cpu, pb + 22, 2, 0xffff);
    return 0;
}

/* The mounted data directory is the virtual volume root. HFS does not create
 * a working-directory record for root (PBOpenWD returns the volume ref).
 * Interface: Inside Macintosh: Files pp. 2-203/204, selector 7, WDPBRec.
 */
static int working_directory(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    if (!rt->file_state) return -35;
    int volume = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    int index = (int16_t)mac_cpu_read(cpu, pb + 26, 2);
    if (volume != 0 && volume != -1)
        return volume <= -16384 ? -51 : -35;
    if (index != 0) return -51; /* No non-root working directories are open. */
    /* get_volume validates the optional name output before any mutation. */
    int error = mac_files_get_volume(rt, pb);
    if (error) return error;
    mac_cpu_write(cpu, pb + 28, 4, 0); /* Root has no working-directory process owner. */
    mac_cpu_write(cpu, pb + 32, 2, 0xffff);
    mac_cpu_write(cpu, pb + 48, 4, 2); /* Root directory on the virtual volume. */
    return 0;
}

static int mac_date(time_t unix_time, uint32_t *out) {
    int64_t value = (int64_t)unix_time + INT64_C(2082844800);
    if (value < 0 || value > UINT32_MAX) return -50;
    *out = (uint32_t)value;
    return 0;
}

/* Count representable direct children, never traversing links. The directory
 * descriptor pins the mounted root even if its original pathname is renamed. */
static int root_count(FileState *s, unsigned *count) {
    int fd = openat(s->directory, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -36;
    DIR *dir = fdopendir(fd);
    if (!dir) { close(fd); return -36; }
    unsigned scanned = 0;
    int error = 0;
    *count = 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(dir);
        if (!e) { if (errno) error = -36; break; }
        if (++scanned > 4096) { error = -50; break; }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        size_t length = strlen(e->d_name);
        int supported = length > 0 && length <= 255;
        for (size_t i = 0; i < length; i++)
            if ((unsigned char)e->d_name[i] < 32 || (unsigned char)e->d_name[i] >= 127 ||
                e->d_name[i] == ':' || e->d_name[i] == '\\') supported = 0;
        if (!supported) continue;
        struct stat st;
        if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) { error = -36; break; }
        if (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode)) (*count)++;
    }
    closedir(dir);
    return error;
}

/* Root-only PBGetCatInfo, documented directory variant offsets, Files 2-295.
 * Unknown Finder and backup metadata is absent (zero), never synthesized from
 * application names. Named entries and indexed catalog enumeration unsupported. */
static int root_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    int volume = (int16_t)mac_cpu_read(cpu,pb+22,2);
    if (volume != 0 && volume != -1) return -35;
    if ((int16_t)mac_cpu_read(cpu,pb+28,2) >= 0) return -50;
    if (mac_cpu_read(cpu,pb+48,4) != 2) return -120;
    uint32_t name = mac_cpu_read(cpu,pb+18,4);
    if (name && !valid(cpu,name,11)) return -50;
    struct stat st;
    if (fstat(s->directory,&st)) return -36;
    uint32_t created = 0, modified;
    if (mac_date(st.st_mtime,&modified)) return -50;
#if defined(__APPLE__)
    if (mac_date(st.st_birthtimespec.tv_sec,&created)) return -50;
#endif
    unsigned count, overlay;
    int error = root_count(s,&count);
    if (!error) error = mac_files_overlay_count(s,&overlay);
    if (error) return error;
    count += overlay;
    /* All input, host operations and output bounds checked before guest writes. */
    if (name) {
        mac_cpu_ram(cpu)[name] = 10;
        memcpy(mac_cpu_ram(cpu)+name+1,"OS Pi Data",10);
    }
    mac_cpu_write(cpu,pb+30,1,0x10); /* Directory, no fabricated Finder lock. */
    mac_cpu_write(cpu,pb+31,1,0); /* Non-AFP access-rights field. */
    memset(mac_cpu_ram(cpu)+pb+32,0,16);
    mac_cpu_write(cpu,pb+48,4,2);
    mac_cpu_write(cpu,pb+52,2,count);
    mac_cpu_write(cpu,pb+72,4,created);
    mac_cpu_write(cpu,pb+76,4,modified);
    mac_cpu_write(cpu,pb+80,4,0);
    memset(mac_cpu_ram(cpu)+pb+84,0,16);
    mac_cpu_write(cpu,pb+100,4,1); /* Virtual parent of volume root. */
    return 0;
}

/* Shared by PBOpen and FSpOpenDF: opens the named regular file into a free slot and returns its reference. */
int mac_files_open_name(MacRuntime *rt, const char *name, unsigned permission, int *ref) {
    FileState *s = rt->file_state;
    if (!s) return -35; /* nsvErr */
    if (rt->trace) fprintf(stderr, "file-open name=%s permission=%u\n", name, permission);
    int index;
    for (index = 0; index < FILE_LIMIT; index++) if (s->files[index].fd < 0) break;
    if (index == FILE_LIMIT) return -42; /* tmfoErr */
    /* Save files shadow data files of the same name and may be writable. */
    int error = mac_files_open_save(rt, name, permission, &s->files[index]);
    if (error < 0) return error;
    if (error == 0) {
        *ref = REF_BASE + index;
        if (rt->trace) fprintf(stderr, "file-open-result name=%s ref=%d writable=%d\n", name, *ref, s->files[index].writable);
        return 0;
    }
    if (permission > 1) return -54; /* permErr: application data is read-only */
    /* No symlink traversal, including within the root. The directory descriptor
     * pins the selected directory; pathname races cannot redirect this open. */
    int fd = openat(s->directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int error = errno == ENOENT ? -43 : errno == ELOOP || errno == EACCES ? -54 : -36;
        if (rt->trace) fprintf(stderr, "file-open-result name=%s error=%d\n", name, error);
        return error;
    }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > INT32_MAX) {
        close(fd); return -50;
    }
    s->files[index] = (GuestFile){fd, 0, 0, {0}};
    memcpy(s->files[index].name, name, strlen(name) < FILE_NAME_MAX ? strlen(name) + 1 : 0);
    *ref = REF_BASE + index;
    if (rt->trace) fprintf(stderr, "file-open-result name=%s ref=%d bytes=%lld\n", name, *ref, (long long)st.st_size);
    return 0;
}

static int open_file(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    int volume = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    unsigned permission = mac_cpu_read(cpu, pb + 27, 1);
    char name[256];
    int error = mac_files_read_name(cpu, mac_cpu_read(cpu, pb + 18, 4), name, sizeof name);
    if (error) return error;
    if (!rt->file_state || (volume != 0 && volume != -1)) return -35; /* nsvErr */
    int ref;
    if ((error = mac_files_open_name(rt, name, permission, &ref))) return error;
    mac_cpu_write(cpu, pb + 24, 2, (uint16_t)ref);
    return 0;
}

static GuestFile *file_ref(MacRuntime *rt, uint32_t pb) {
    int ref = (int16_t)mac_cpu_read(rt->cpu, pb + 24, 2) - REF_BASE;
    FileState *s = rt->file_state;
    return s && ref >= 0 && ref < FILE_LIMIT && s->files[ref].fd >= 0 ? &s->files[ref] : NULL;
}

/* PBGetEOF: File Manager reference 2-127. The pinned descriptor, rather
 * than a pathname or cached open-time size, identifies the requested file.
 * ioMisc (+28) is the logical length; the file mark is never changed. */
static int get_eof(MacRuntime *rt, uint32_t pb, GuestFile *f) {
    struct stat st;
    if (fstat(f->fd, &st) || !S_ISREG(st.st_mode) ||
        st.st_size < 0 || st.st_size > INT32_MAX) return -36;
    mac_cpu_write(rt->cpu, pb + 28, 4, (uint32_t)st.st_size);
    return 0;
}

/* PBFlushVol, Inside Macintosh: Files 2-143. Volume-name precedence follows
 * the standard volume selector (Inside Macintosh II-99). Both our volumes are
 * immutable to guests: no dirty volume/FCB buffers exist to write. Do not fsync
 * an unrelated host disk or claim persistence for future writable volumes. */
static int flush_volume(MacRuntime *rt, uint32_t pb) {
    MacCpu *c = rt->cpu;
    int volume = (int16_t)mac_cpu_read(c, pb + 22, 2);
    uint32_t p = mac_cpu_read(c, pb + 18, 4);
    if (p) {
        if (!valid(c,p,1) || !valid(c,p,1 + mac_cpu_read(c,p,1))) return -50;
        unsigned n = mac_cpu_read(c,p,1), end = 0;
        const uint8_t *name = mac_cpu_ram(c)+p+1;
        while (end < n && name[end] != ':') end++;
        if (end && end < n) {
            static const char *names[] = {"OS Pi Data", "OS Pi System"};
            volume = -3;
            for (unsigned i=0;i<2;i++) {
                if (end != strlen(names[i])) continue;
                unsigned j=0;
                for (;j<end;j++) {
                    unsigned a=name[j], b=(unsigned char)names[i][j];
                    if (a>='A' && a<='Z') a+='a'-'A';
                    if (b>='A' && b<='Z') b+='a'-'A';
                    if (a!=b) break;
                }
                if (j==end) volume=-(int)i-1;
            }
        }
    }
    if (volume==2) return rt->system_volume && mac_drive_number(rt,2)?0:-56;
    if (volume==-2) return rt->system_volume?0:-35;
    if (volume==0 && !rt->file_state) return -50;
    if (volume!=0 && volume!=-1 && volume!=1) return volume>0?-56:-35;
    FileState *state=rt->file_state;
    if (!state) return volume==1?-56:-35;
    struct stat st;
    if (fstat(state->directory,&st) || !S_ISDIR(st.st_mode)) return -36;
    return 0;
}

static int read_file(MacRuntime *rt, uint32_t pb, GuestFile *f) {
    MacCpu *cpu = rt->cpu;
    mac_cpu_write(cpu, pb + 40, 4, 0);
    int32_t count = (int32_t)mac_cpu_read(cpu, pb + 36, 4);
    uint32_t buffer = mac_cpu_read(cpu, pb + 32, 4);
    unsigned mode = mac_cpu_read(cpu, pb + 44, 2);
    int32_t offset = (int32_t)mac_cpu_read(cpu, pb + 46, 4);
    /* Cache hints are harmless on the host. Newline/device modes unsupported. */
    if (count < 0 || !valid(cpu, buffer, (uint32_t)count) || (mode & ~0x33u)) return -50;
    struct stat st;
    if (fstat(f->fd, &st) || st.st_size > INT32_MAX) return -36;
    int64_t mark = f->mark;
    switch (mode & 3u) {
    case 1: mark = offset; break;
    case 2: mark = st.st_size + (int64_t)offset; break;
    case 3: mark += offset; break;
    }
    if (mark < 0) return -40;
    if (mark > INT32_MAX) return -50;
    /* PBRead cannot extend a read-only file; retain EOF as the resulting mark. */
    if (mark > st.st_size) mark = st.st_size;
    f->mark = (int32_t)mark;
    uint32_t limit = (uint32_t)(st.st_size - mark);
    if (limit > (uint32_t)count) limit = (uint32_t)count;
    uint32_t actual = 0;
    int error = 0;
    while (actual < limit) {
        ssize_t got = pread(f->fd, mac_cpu_ram(cpu) + buffer + actual,
                            (size_t)limit - actual, mark + actual);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) { error = -36; break; }
        if (!got) { error = -39; break; }
        actual += (uint32_t)got;
    }
    if (!error && actual < (uint32_t)count) error = -39;
    f->mark += (int32_t)actual;
    mac_cpu_write(cpu, pb + 40, 4, actual);
    mac_cpu_write(cpu, pb + 46, 4, (uint32_t)f->mark);
    return error;
}

/* Parameter block bytes the synchronous call may touch; validated before any
 * guest read or write. Async variants (bit 10) are rejected with paramErr. */
static unsigned block_size(unsigned base, int wd, unsigned selector) {
    switch (base) {
    case 0xa011: case 0xa012: return 32;
    case 0xa013: case 0xa014: case 0xa015: return 24;
    case 0xa00c: case 0xa00d: case 0xa20c: return 80;
    case 0xa007: return 64;
    case 0xa209: case 0xa214: case 0xa215: case 0xa00a: case 0xa20a: return 52;
    case 0xa207: return 122;
    case 0xa260: return wd && selector == 9 ? 104 : 52;
    default: return PB_BYTES;
    }
}

int mac_file_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode == 0xaa52) return mac_fsspec_service(rt, opcode); /* HighLevelFSDispatch */
    unsigned base = opcode & ~0x400u;
    switch (base) {
    case 0xa000: case 0xa001: case 0xa002: case 0xa003: case 0xa007: case 0xa008: case 0xa009:
    case 0xa00b: case 0xa00c: case 0xa00d: case 0xa011: case 0xa012: case 0xa013:
    case 0xa014: case 0xa015: case 0xa018: case 0xa044: case 0xa207: case 0xa209:
    case 0xa060: case 0xa20c: case 0xa260: case 0xa00a: case 0xa20a: case 0xa214: case 0xa215: break;
    default: return 0;
    }
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    int wd = base == 0xa260;
    unsigned selector = mac_cpu_get(cpu, MAC_D0) & 0xffff;
    if (wd && selector == 6) {   /* DirCreate: this volume has no folders (flat namespace), which wrgVolTypErr (-123, "not an HFS volume") says; nothing is created */
        uint32_t q = mac_cpu_get(cpu, MAC_A0);
        if (!(q & 1) && valid(cpu, q, 52)) return result(cpu, q, -123);
    }
    if (wd && selector != 7 && selector != 9) return 0;
    if (base == 0xa060 && selector != 0x1a) return 0; /* _FSDispatch: only PBOpenDF so far */
    uint32_t pb = mac_cpu_get(cpu, MAC_A0);
    if ((pb & 1) || !valid(cpu, pb, block_size(base, wd, selector))) {
        mac_cpu_halt(cpu, "Invalid File Manager parameter block."); return -1;
    }
    if (opcode & 0x400) return result(cpu, pb, -50); /* async unsupported */
    FileState *state = rt->file_state;
    switch (base) {
    case 0xa013: return result(cpu, pb, flush_volume(rt, pb));
    case 0xa007: return result(cpu, pb, mac_volume_info_basic(rt, pb, state ? state->directory : -1));
    case 0xa207: return result(cpu, pb, mac_volume_info(rt, pb, state ? state->directory : -1));
    case 0xa260: {
        int error = selector == 9 ? root_info(rt,pb) : working_directory(rt,pb);
        if (rt->trace) fprintf(stderr, "hfs-dispatch selector=%u volume=%d index=%d dirid=%u name-ptr=%08x error=%d\n",
                               selector, (int16_t)mac_cpu_read(cpu,pb+22,2), (int16_t)mac_cpu_read(cpu,pb+28,2),
                               mac_cpu_read(cpu,pb+48,4), mac_cpu_read(cpu,pb+18,4), error);
        return result(cpu, pb, error);
    }
    case 0xa014: return result(cpu, pb, mac_files_get_volume(rt, pb));
    case 0xa214: case 0xa215: return result(cpu, pb, mac_files_hvol(rt, pb, base == 0xa214)); /* PBHGetVol, PBHSetVol */
    case 0xa015: return result(cpu, pb, mac_files_set_vol(rt, pb));
    case 0xa000: case 0xa060: return result(cpu, pb, open_file(rt, pb)); /* PBOpen, and PBOpenDF (data fork) */
    case 0xa00a: case 0xa20a: return result(cpu, pb, mac_files_open_rf(rt, pb)); /* PBOpenRF, PBHOpenRF: the .rsrc sidecar */
    case 0xa008: return result(cpu, pb, mac_files_create(rt, pb));
    case 0xa009: return result(cpu, pb, mac_files_delete(rt, pb, 0));
    case 0xa209: return result(cpu, pb, mac_files_delete(rt, pb, 1));
    case 0xa00b: return result(cpu, pb, mac_files_rename(rt, pb));
    case 0xa00c: return result(cpu, pb, mac_files_get_info(rt, pb));
    case 0xa20c: return result(cpu, pb, mac_files_hget_info(rt, pb));
    case 0xa00d: return result(cpu, pb, mac_files_set_info(rt, pb));
    default: break;
    }
    if (base == 0xa002 || base == 0xa003) mac_cpu_write(cpu, pb + 40, 4, 0);
    GuestFile *f = file_ref(rt, pb);
    if (!f) return result(cpu, pb, -51);
    switch (base) {
    case 0xa002: return result(cpu, pb, read_file(rt, pb, f));
    case 0xa003: return result(cpu, pb, mac_files_write(rt, pb, f));
    case 0xa011: return result(cpu, pb, get_eof(rt, pb, f));
    case 0xa012: return result(cpu, pb, mac_files_set_eof(rt, pb, f));
    case 0xa018: return result(cpu, pb, mac_files_set_fpos(rt, pb, f, 1));
    case 0xa044: return result(cpu, pb, mac_files_set_fpos(rt, pb, f, 0));
    default: return result(cpu, pb, mac_files_close(state, f));
    }
}
