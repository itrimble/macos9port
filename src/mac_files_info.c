#include "mac_files_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* PBGetFInfo/PBSetFInfo and PBSetVol for the virtual data volume and its save
 * overlay (checklist L04). Independent implementation of the documented
 * FileParam layout (Inside Macintosh: Files, chapter 2). Entries are looked up
 * by pinned directory descriptor; links and directories are never reported. */
enum { ENTRY_LIMIT = 4096, LIST_LIMIT = 1024 };

static int mac_date(time_t unix_time, uint32_t *out) {
    int64_t value = (int64_t)unix_time + INT64_C(2082844800);
    if (value < 0 || value > UINT32_MAX) return -50;
    *out = (uint32_t)value;
    return 0;
}

static int compare_names(const void *a, const void *b) { return strcmp(a, b); }

/* Sorted merged listing of regular files: data volume first, then saves that
 * do not shadow a data name. Bounded; deterministic for indexed PBGetFInfo. */
static int listing(const FileState *s, char (**out)[256], unsigned *count) {
    char (*names)[256] = calloc(LIST_LIMIT, sizeof *names);
    if (!names) return -108;
    *count = 0;
    int error = 0;
    for (int pass = 0; pass < 2 && !error; pass++) {
        int directory = pass ? s->saves : s->directory;
        if (directory < 0) continue;
        DIR *dir = mac_files_open_listing(directory);
        if (!dir) { error = -36; break; }
        unsigned scanned = 0;
        for (;;) {
            errno = 0;
            struct dirent *e = readdir(dir);
            if (!e) { if (errno) error = -36; break; }
            if (++scanned > ENTRY_LIMIT || *count >= LIST_LIMIT) { error = -50; break; }
            size_t length = strlen(e->d_name);
            if (!length || length > 255 || !strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (pass && !mac_files_save_name_ok(e->d_name)) continue;
            int supported = 1;
            for (size_t i = 0; i < length; i++)
                if ((unsigned char)e->d_name[i] < 32 || (unsigned char)e->d_name[i] >= 127 ||
                    e->d_name[i] == ':' || e->d_name[i] == '\\') supported = 0;
            struct stat st;
            if (!supported || fstatat(directory, e->d_name, &st, AT_SYMLINK_NOFOLLOW) || !S_ISREG(st.st_mode)) continue;
            if (pass && !fstatat(s->directory, e->d_name, &st, AT_SYMLINK_NOFOLLOW)) continue;
            memcpy(names[(*count)++], e->d_name, length + 1);
        }
        closedir(dir);
    }
    if (error) { free(names); return error; }
    qsort(names, *count, sizeof *names, compare_names);
    *out = names;
    return 0;
}

int mac_files_get_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    int error = mac_files_volume_ok(cpu, pb);
    if (error) return error;
    int index = (int16_t)mac_cpu_read(cpu, pb + 28, 2);
    uint32_t name_ptr = mac_cpu_read(cpu, pb + 18, 4);
    char name[256];
    if (index > 0) {
        char (*names)[256];
        unsigned count;
        if ((error = listing(s, &names, &count))) return error;
        if ((unsigned)index > count) { free(names); return -43; }
        memcpy(name, names[index - 1], strlen(names[index - 1]) + 1);
        free(names);
        if (name_ptr && !mac_files_valid(cpu, name_ptr, (uint32_t)strlen(name) + 1)) return -50;
    } else if ((error = mac_files_read_name(cpu, name_ptr, name, sizeof name))) return error;
    int kind = mac_files_classify(s, name);
    if (kind != FILE_SAVE && kind != FILE_DATA) return -43;
    int directory = kind == FILE_SAVE ? s->saves : s->directory;
    int fd = mac_files_open_regular(directory, name, O_RDONLY);
    if (fd < 0) return fd == -43 ? -43 : -36;
    struct stat st;
    uint8_t info[16] = {0};
    uint32_t created = 0, modified;
    if (fstat(fd, &st) || mac_date(st.st_mtime, &modified)) { close(fd); return -36; }
#if defined(__APPLE__)
    if (mac_date(st.st_birthtimespec.tv_sec, &created)) created = modified;
#else
    created = modified;
#endif
    mac_files_finder_get(fd, info);
    close(fd);
    uint32_t length = (uint32_t)st.st_size, physical = (length + 511u) & ~511u;
    /* All host work succeeded; now publish to the guest block. */
    if (index > 0 && name_ptr) {
        mac_cpu_ram(cpu)[name_ptr] = (uint8_t)strlen(name);
        memcpy(mac_cpu_ram(cpu) + name_ptr + 1, name, strlen(name));
    }
    mac_cpu_write(cpu, pb + 30, 1, kind == FILE_DATA ? 0x01 : 0x00); /* Data files are locked. */
    mac_cpu_write(cpu, pb + 31, 1, 0);
    memcpy(mac_cpu_ram(cpu) + pb + 32, info, 16);
    mac_cpu_write(cpu, pb + 48, 4, (uint32_t)st.st_ino ? (uint32_t)st.st_ino : 1u);
    mac_cpu_write(cpu, pb + 52, 2, 0);
    mac_cpu_write(cpu, pb + 54, 4, length);
    mac_cpu_write(cpu, pb + 58, 4, physical);
    mac_cpu_write(cpu, pb + 62, 2, 0);
    mac_cpu_write(cpu, pb + 64, 4, 0);
    mac_cpu_write(cpu, pb + 68, 4, 0);
    mac_cpu_write(cpu, pb + 72, 4, created);
    mac_cpu_write(cpu, pb + 76, 4, modified);
    return 0;
}

/* PBSetFInfo stores the 16-byte Finder info on the committed save. Dates in
 * the block are accepted but not applied to the host file. */
int mac_files_set_info(MacRuntime *rt, uint32_t pb) {
    FileState *s = rt->file_state;
    char name[256];
    int error = mac_files_read_name(rt->cpu, mac_cpu_read(rt->cpu, pb + 18, 4), name, sizeof name);
    if (error) return error;
    if (!s) return -35;
    if ((error = mac_files_volume_ok(rt->cpu, pb))) return error;
    if ((error = mac_files_locked_error(s, mac_files_classify(s, name)))) return error;
    int fd = mac_files_open_regular(s->saves, name, O_RDWR);
    if (fd < 0) return fd;
    error = mac_files_finder_set(fd, mac_cpu_ram(rt->cpu) + pb + 32);
    close(fd);
    return error;
}

/* PBSetVol: only the mounted data volume exists, so the call validates the
 * request and leaves the default volume unchanged. */
int mac_files_set_vol(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    if (!rt->file_state) return -35;
    uint32_t p = mac_cpu_read(cpu, pb + 18, 4);
    if (p) {
        if (!mac_files_valid(cpu, p, 1)) return -50;
        unsigned n = mac_cpu_read(cpu, p, 1), end = 0;
        if (!mac_files_valid(cpu, p + 1, n)) return -50;
        const uint8_t *name = mac_cpu_ram(cpu) + p + 1;
        while (end < n && name[end] != ':') end++;
        if (end && end < n) {
            static const char volume[] = "OS Pi Data";
            if (end != sizeof volume - 1) return -35;
            for (unsigned i = 0; i < end; i++) {
                unsigned a = name[i], b = (unsigned char)volume[i];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) return -35;
            }
            return 0;
        }
    }
    return mac_files_volume_ok(cpu, pb);
}
