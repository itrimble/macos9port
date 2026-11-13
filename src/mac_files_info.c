#include "mac_files_int.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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

/* Sorted merged listing of a directory: the data volume first, then saves that do not shadow a data name. Bounded; deterministic for indexed calls.
 * Files only unless with_dirs (a directory's name then ends in '/'); a ".rsrc" sidecar is left out when hide_sidecars and its file is there too. */
int mac_files_listing_dir(const FileState *s, const char *dirpath, int with_dirs, int hide_sidecars, char (**out)[256], unsigned *count) {
    char (*names)[256] = calloc(LIST_LIMIT, sizeof *names);
    if (!names) return -108;
    *count = 0;
    int error = 0;
    for (int pass = 0; pass < 2 && !error; pass++) {
        int directory = pass ? s->saves : s->directory;
        if (directory < 0) continue;
        DIR *dir = mac_dirs_opendir(directory, dirpath);
        if (!dir) { if (errno == ENOENT || errno == ENOTDIR) continue; error = -36; break; }
        unsigned scanned = 0;
        for (;;) {
            errno = 0;
            struct dirent *e = readdir(dir);
            if (!e) { if (errno) error = -36; break; }
            if (++scanned > ENTRY_LIMIT || *count >= LIST_LIMIT) { error = -50; break; }
            size_t length = strlen(e->d_name);
            if (!length || length > 254 || e->d_name[0] == '.') continue;   /* hidden names stay hidden */
            int supported = 1;
            for (size_t i = 0; i < length; i++)
                if ((unsigned char)e->d_name[i] < 32 || (unsigned char)e->d_name[i] >= 127 ||
                    e->d_name[i] == ':' || e->d_name[i] == '\\') supported = 0;
            char child[DIRS_PATH_MAX];
            if (!supported || snprintf(child, sizeof child, "%s%s%s", dirpath, dirpath[0] ? "/" : "", e->d_name) >= (int)sizeof child) continue;
            struct stat st;
            if (mac_dirs_stat(directory, child, &st)) continue;
            int is_dir = S_ISDIR(st.st_mode);
            if (!(S_ISREG(st.st_mode) || (is_dir && with_dirs))) continue;
            if (pass && !mac_dirs_stat(s->directory, child, &st)) continue;   /* shadows (or is) the data volume's entry */
            if (hide_sidecars && !is_dir && length > 5 && !strcmp(e->d_name + length - 5, ".rsrc")) {
                char base[DIRS_PATH_MAX]; snprintf(base, sizeof base, "%s", child); base[strlen(base) - 5] = 0;
                if (mac_files_classify(s, base) != FILE_NONE) continue;
            }
            memcpy(names[*count], e->d_name, length + 1);
            if (is_dir) { names[*count][length] = '/'; names[*count][length + 1] = 0; }
            (*count)++;
        }
        closedir(dir);
    }
    if (error) { free(names); return error; }
    qsort(names, *count, sizeof *names, compare_names);
    *out = names;
    return 0;
}
static int listing(const FileState *s, char (**out)[256], unsigned *count) { return mac_files_listing_dir(s, "", 0, 1, out, count); }

int mac_files_entries(MacRuntime *rt, MacFileEntry *out, unsigned max, unsigned *count) {
    FileState *s = rt->file_state;
    *count = 0;
    if (!s) return -35;
    mac_files_flush_staged(s);
    char (*names)[256];
    unsigned total;
    int error = listing(s, &names, &total);
    if (error) return error;
    if (total > max) { free(names); return -50; }
    for (unsigned i = 0; i < total; i++) {
        int kind = mac_files_classify(s, names[i]);
        if (kind != FILE_SAVE && kind != FILE_DATA) continue;
        int fd = mac_files_open_regular(kind == FILE_SAVE ? s->saves : s->directory, names[i], O_RDONLY);
        if (fd < 0) continue; /* vanished or unreadable between listing and open: not offered */
        uint8_t info[16] = {0};
        mac_files_finder_get(fd, info);
        close(fd);
        MacFileEntry *e = &out[*count];
        memcpy(e->name, names[i], sizeof e->name);
        e->type = (uint32_t)info[0] << 24 | (uint32_t)info[1] << 16 | (uint32_t)info[2] << 8 | info[3];
        e->creator = (uint32_t)info[4] << 24 | (uint32_t)info[5] << 16 | (uint32_t)info[6] << 8 | info[7];
        e->flags = (uint16_t)(info[8] << 8 | info[9]);
        (*count)++;
    }
    free(names);
    return 0;
}

/* PBHGetFInfo ($A20C): the same lookup and indexed listing as PBGetFInfo, in the directory ioDirID (offset 48, an input that is left as the caller set it,
 * where PBGetFInfo would store a file number there). The index counts files only (PBGetCatInfo counts directories too). */
int mac_files_hget_info(MacRuntime *rt, uint32_t pb) {
    uint32_t dir = mac_cpu_read(rt->cpu, pb + 48, 4);
    int error = mac_files_get_info(rt, pb);
    mac_cpu_write(rt->cpu, pb + 48, 4, dir);
    return error;
}

/* The file record shared by PBGetFInfo and PBGetCatInfo: bytes 30..79 of the parameter block for the regular file at `path` (kind FILE_SAVE or FILE_DATA). */
int mac_files_fill_info(MacRuntime *rt, uint32_t pb, const char *path, int kind) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    int directory = kind == FILE_SAVE ? s->saves : s->directory;
    int fd = mac_files_open_regular(directory, path, O_RDONLY);
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

int mac_files_get_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    mac_files_flush_staged(s);                                  /* what an open file has written is visible to a lookup */
    int error = mac_files_volume_ok_rt(rt, pb);
    if (error) return error;
    int index = (int16_t)mac_cpu_read(cpu, pb + 28, 2);
    uint32_t name_ptr = mac_cpu_read(cpu, pb + 18, 4);
    char path[DIRS_PATH_MAX], shown[256] = "";
    if (index > 0) {   /* the index-th file of the directory; ioNamePtr is an output */
        char (*names)[256];
        unsigned count;
        if ((error = mac_files_pb_path(rt, pb, 0, path, 1))) return error;
        if (!mac_dirs_is_dir(s, path)) return -120;
        if ((error = mac_files_listing_dir(s, path, 0, 1, &names, &count))) return error;
        if ((unsigned)index > count) { free(names); return -43; }
        snprintf(shown, sizeof shown, "%s", names[index - 1]);
        free(names);
        if (name_ptr && !mac_files_valid(cpu, name_ptr, (uint32_t)strlen(shown) + 1)) return -50;
        size_t have = strlen(path);
        if (have + strlen(shown) + 2 >= sizeof path) return -37;
        snprintf(path + have, sizeof path - have, "%s%s", have ? "/" : "", shown);
    } else if ((error = mac_files_pb_path(rt, pb, name_ptr, path, 0))) return error;
    int kind = mac_files_classify(s, path);
    if (kind != FILE_SAVE && kind != FILE_DATA) return -43;
    if ((error = mac_files_fill_info(rt, pb, path, kind))) return error;
    if (index > 0 && name_ptr) {   /* All host work succeeded; now publish the name. */
        mac_cpu_ram(cpu)[name_ptr] = (uint8_t)strlen(shown);
        memcpy(mac_cpu_ram(cpu) + name_ptr + 1, shown, strlen(shown));
    }
    return 0;
}

/* PBSetFInfo stores the 16-byte Finder info on the committed save. Dates in
 * the block are accepted but not applied to the host file. */
int mac_files_set_info(MacRuntime *rt, uint32_t pb) {
    FileState *s = rt->file_state;
    char name[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(rt->cpu, pb + 18, 4), name, 0);
    if (error) return error;
    if (!s) return -35;
    if ((error = mac_files_volume_ok_rt(rt, pb))) return error;
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
            static const char volume[] = "OS999 HD";
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
    return mac_files_volume_ok_rt(rt, pb);
}
