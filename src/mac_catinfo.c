#include "mac_files_int.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* PBGetCatInfo (HFSDispatch selector 9) and PBCatMove (selector 5) over the hierarchical volume, from the public Files chapter in my own words.
 * CInfoPBRec offsets: ioNamePtr +18, ioVRefNum +22, ioFDirIndex +28, ioFlAttrib +30 (bit 4 set for a directory), ioACUser +31, the Finder info +32 (16 bytes),
 * ioDirID / ioDrDirID / file number +48, directory valence +52 (2 bytes), dates +72 (created) +76 (modified) +80 (backup), extended Finder info +84
 * (16 bytes), ioFlParID / ioDrParID +100. The item is chosen by ioFDirIndex: greater than 0 the n-th entry (files and directories, name order) of the
 * directory ioDirID, written back with its name through ioNamePtr; 0 the entry ioNamePtr names in ioDirID (a bare name or a partial path); less than 0
 * the directory ioDirID itself, whose name is written through ioNamePtr when it is not nil. The volume root answers as directory 2 with parent 1 and
 * the volume's name. Entries are the merged view of the save overlay and the read-only application data; a ".rsrc" sidecar whose file is present is
 * the file's resource fork, not a separate entry. Dates come from the host; backup dates, access rights and extended Finder info are zero. */
static int mac_date(time_t unix_time, uint32_t *out) {
    int64_t value = (int64_t)unix_time + INT64_C(2082844800);
    if (value < 0 || value > UINT32_MAX) return -50;
    *out = (uint32_t)value;
    return 0;
}

static int put_name(MacCpu *cpu, uint32_t ptr, const char *name) {
    int n = mr_host_length(name);   /* the name goes out in Mac Roman */
    if (n < 0 || !mac_files_valid(cpu, ptr, (uint32_t)n + 1)) return -50;
    return mr_put_pstring(mac_cpu_ram(cpu) + ptr, MR_NAME_MAX, name);
}

static int directory_record(MacRuntime *rt, uint32_t pb, const char *path) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    struct stat st;
    int root = s->saves >= 0 && !path[0] ? s->saves : s->directory;
    if (path[0] && s->saves >= 0 && !mac_dirs_stat(s->saves, path, &st) && S_ISDIR(st.st_mode)) root = s->saves;
    if (path[0] ? mac_dirs_stat(root, path, &st) : fstat(s->directory, &st)) return -36;
    uint32_t created = 0, modified;
    if (mac_date(st.st_mtime, &modified)) return -50;
#if defined(__APPLE__)
    if (mac_date(st.st_birthtimespec.tv_sec, &created)) created = modified;
#endif
    char (*names)[256]; unsigned count;
    int error = mac_files_listing_dir(s, path, 1, 1, &names, &count);
    if (error) return error;
    free(names);
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(path, parent, &leaf);
    mac_cpu_write(cpu, pb + 30, 1, 0x10);                                 /* a directory; no fabricated lock */
    mac_cpu_write(cpu, pb + 31, 1, 0);
    memset(mac_cpu_ram(cpu) + pb + 32, 0, 16);
    if (path[0]) {   /* a folder's Finder info, as PBSetCatInfo stored it in the overlay (or as the data volume has it) */
        int fd = mac_dirs_openat(root, path, O_RDONLY, 0);
        if (fd >= 0) { uint8_t info[16]; if (mac_files_finder_get(fd, info)) memcpy(mac_cpu_ram(cpu) + pb + 32, info, 16); close(fd); }
    }
    mac_cpu_write(cpu, pb + 48, 4, mac_dirs_id(&s->dirs, path));
    mac_cpu_write(cpu, pb + 52, 2, count > 0xffff ? 0xffff : count);
    memset(mac_cpu_ram(cpu) + pb + 54, 0, 18);
    mac_cpu_write(cpu, pb + 72, 4, created);
    mac_cpu_write(cpu, pb + 76, 4, modified);
    mac_cpu_write(cpu, pb + 80, 4, 0);
    memset(mac_cpu_ram(cpu) + pb + 84, 0, 16);
    mac_cpu_write(cpu, pb + 100, 4, path[0] ? mac_dirs_id(&s->dirs, parent) : 1);   /* the root's parent is the virtual 1 */
    return 0;
}

int mac_files_cat_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    s->hfs_call = 1;
    mac_files_flush_staged(s);
    if (!mac_files_valid(cpu, pb, 104)) return -50;
    int index = (int16_t)mac_cpu_read(cpu, pb + 28, 2);
    uint32_t name_ptr = mac_cpu_read(cpu, pb + 18, 4);
    char path[DIRS_PATH_MAX], shown[256] = "";
    int error;
    if (index > 0) {
        char (*names)[256]; unsigned count;
        if ((error = mac_files_pb_path(rt, pb, 0, path, 1))) return error;
        if (!mac_dirs_is_dir(s, path)) return -120;
        if ((error = mac_files_listing_dir(s, path, 1, 1, &names, &count))) return error;
        if ((unsigned)index > count) { free(names); return -43; }
        snprintf(shown, sizeof shown, "%s", names[index - 1]);
        free(names);
        size_t n = strlen(shown);
        if (shown[n - 1] == '/') shown[n - 1] = 0;
        size_t have = strlen(path);
        if (have + strlen(shown) + 2 >= sizeof path) return -37;
        snprintf(path + have, sizeof path - have, "%s%s", have ? "/" : "", shown);
    } else if (index < 0) {
        if ((error = mac_files_pb_path(rt, pb, 0, path, 1))) return error;
        if (!mac_dirs_is_dir(s, path)) return -120;
        const char *leaf; char parent[DIRS_PATH_MAX];
        mac_dirs_parent(path, parent, &leaf);
        snprintf(shown, sizeof shown, "%s", path[0] ? leaf : "OS999 HD");
    } else if ((error = mac_files_pb_path(rt, pb, name_ptr, path, 1))) return error;
    if (name_ptr && index != 0 && !mac_files_valid(cpu, name_ptr, (uint32_t)strlen(shown) + 1)) return -50;
    if (mac_dirs_is_dir(s, path)) {
        if ((error = directory_record(rt, pb, path))) return error;
    } else {
        int kind = mac_files_classify(s, path);
        if (kind != FILE_SAVE && kind != FILE_DATA) return -43;
        char parent[DIRS_PATH_MAX]; const char *leaf;
        mac_dirs_parent(path, parent, &leaf);
        if ((error = mac_files_fill_info(rt, pb, path, kind))) return error;
        mac_cpu_write(cpu, pb + 100, 4, mac_dirs_id(&s->dirs, parent));        /* ioFlParID: the directory it is in (Glider hands it to FSMakeFSSpec) */
    }
    if (name_ptr && index != 0) return put_name(cpu, name_ptr, shown);
    return 0;
}

/* PBCatMove: CMovePBRec ioNamePtr +18 (the item, in ioDirID +48 / ioVRefNum +22), ioNewName +28 and ioNewDirID +36 (the destination directory: a name
 * or partial path in that directory ID; both may be given). The layout is from my memory of the public record, checked only by this project's test.
 * Only items in the save overlay can move; an item of the application data is fLckdErr. */
int mac_files_cat_move(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    s->hfs_call = 1;
    if (!mac_files_valid(cpu, pb, 52)) return -50;
    char from[DIRS_PATH_MAX], dest[DIRS_PATH_MAX], to[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(cpu, pb + 18, 4), from, 0);
    if (error) return error;
    int vref = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    if ((error = mac_files_ptr_path(rt, mac_cpu_read(cpu, pb + 28, 4), vref, mac_cpu_read(cpu, pb + 36, 4), dest))) return error;
    if (!mac_dirs_is_dir(s, dest)) return -120;
    const char *leaf; char parent[DIRS_PATH_MAX];
    mac_dirs_parent(from, parent, &leaf);
    if (snprintf(to, sizeof to, "%s%s%s", dest, dest[0] ? "/" : "", leaf) >= (int)sizeof to) return -37;
    if (!strcmp(from, to)) return 0;
    return mac_files_rename_names(rt, from, to);
}
