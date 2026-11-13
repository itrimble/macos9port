#include "mac_files_int.h"
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* Directories of the guest volume (PBDirCreate, FindFolder's folders, working directories). Directories are real host directories: the application data
 * root is read-only, new directories are made in the save overlay, and a directory that exists in both is one directory whose entries are merged.
 * Directory IDs map to host-relative paths (mac_dirs.h). */

/* A special folder of the guest volume, named by a partial path from the root with ':' between the components ("System Folder:Extensions"): its ID when
 * it exists; otherwise fnfErr (-43), or, with `create`, it and any missing folder above it are made now in the save overlay (-46 without a writable
 * overlay, like DirCreate; -34 when the disk budget is spent). A folder that exists only in the application's read-only data is found as it is. */
int mac_files_folder_find(MacRuntime *rt, const char *name, int create, uint32_t *id) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    char part[DIRS_PATH_MAX], path[DIRS_PATH_MAX];
    size_t length = strlen(name);
    if (!length) return -50;
    for (size_t at = 0; at < length;) {
        const char *colon = strchr(name + at, ':');
        size_t end = colon ? (size_t)(colon - name) : length;
        if (end >= sizeof part) return -50;
        memcpy(part, name, end); part[end] = 0;
        int error = mac_dirs_resolve(rt, -1, DIRS_ROOT_ID, part, path);
        if (error) return error == -37 ? -50 : error;
        if (!mac_dirs_is_dir(s, path)) {
            if (!create) return -43;
            if (s->saves < 0) return -46;
            if (mac_files_classify(s, path) != FILE_NONE) return -48;
            char parent[DIRS_PATH_MAX]; const char *leaf;
            mac_dirs_parent(path, parent, &leaf);
            if (volume_budget_spend(&s->disk, 512)) return -34;
            if (parent[0] && (error = mac_dirs_ensure_overlay(s, parent))) return error;
            if (mac_dirs_mkdir(s->saves, path)) return -36;
        }
        at = end + 1;
    }
    *id = mac_dirs_id(&s->dirs, path);
    return 0;
}

/* PBDirCreate (HFSDispatch selector 6): ioNamePtr +18 (the new directory's name, or a partial path to it), ioVRefNum +22, ioDirID +48 (the parent, 0 for
 * the default directory; the new directory's ID on return). Errors: -46 no writable save overlay, -35 unknown volume, -120 a parent that does not exist,
 * -37 / -50 a bad name or pointer, -48 the name is taken by a file or directory, -34 the volume is full. */
int mac_files_dir_create(MacRuntime *rt, uint32_t pb) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    s->hfs_call = 1;
    if (s->saves < 0) return -46;
    char path[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(rt->cpu, pb + 18, 4), path, 0);
    if (error) return error;
    if (!mac_files_save_name_ok(path)) return -37;
    if (mac_files_classify(s, path) != FILE_NONE || mac_dirs_is_dir(s, path)) return -48;
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(path, parent, &leaf);
    unsigned files; uint64_t bytes;
    if ((error = mac_files_usage(s, &files, &bytes))) return error == -50 ? -34 : error;   /* a volume with too many entries is full */
    if (volume_budget_spend(&s->disk, 512)) return -34;
    if (parent[0] && (error = mac_dirs_ensure_overlay(s, parent))) return error;
    if (mac_dirs_mkdir(s->saves, path)) return errno == EEXIST ? -48 : -36;
    fsync(s->saves);
    mac_cpu_write(rt->cpu, pb + 48, 4, mac_dirs_id(&s->dirs, path));
    return 0;
}

int mac_files_volume_ok_rt(MacRuntime *rt, uint32_t pb) {
    int vref = (int16_t)mac_cpu_read(rt->cpu, pb + 22, 2);
    if ((vref == -2 && rt->system_volume) || mac_files_wd_folder(rt, vref)) return 0;
    return mac_files_volume_ok(rt->cpu, pb);
}

/* Working directories (HFSDispatch selectors 1 PBOpenWD, 2 PBCloseWD, 7 PBGetWDInfo): the reference of an open working directory is -(100 + slot) in a
 * small table (mac_dirs.h). Any name call accepts it as ioVRefNum: the name is then in that directory. */
uint32_t mac_files_wd_folder(const MacRuntime *rt, int vref) {
    const FileState *s = rt ? rt->file_state : NULL;
    return s ? mac_dirs_wd_dir(&s->dirs, vref) : 0;
}
/* Whether PBOpenWD / PBCloseWD / PBGetWDInfo names something of ours (so the System volume's own handler leaves it alone): a directory other than the
 * root by ID for selector 1, an open working-directory reference for 2 and 7. */
int mac_files_wd_ours(const MacRuntime *rt, unsigned selector, uint32_t dir_id, int vref) {
    if (selector == 1) return rt && rt->file_state && dir_id > DIRS_ROOT_ID;
    return (selector == 2 || selector == 7) && mac_files_wd_folder(rt, vref) != 0;
}
int mac_files_wd(MacRuntime *rt, uint32_t pb, unsigned selector) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    int vref = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    if (selector == 1) {
        uint32_t id = mac_cpu_read(cpu, pb + 48, 4);
        char path[DIRS_PATH_MAX];
        if (!(vref == 0 || vref == -1 || (vref == -2 && rt->system_volume))) return -35;
        if (mac_dirs_path(&s->dirs, id, path) || !mac_dirs_is_dir(s, path)) return -120;
        int ref = mac_dirs_wd_open(&s->dirs, id);
        if (!ref) return -42;   /* tmwdoErr: too many working directories open */
        mac_cpu_write(cpu, pb + 22, 2, (uint16_t)ref);
        return 0;
    }
    uint32_t id = mac_files_wd_folder(rt, vref);
    if (!id) return -51;                                              /* rfNumErr: not an open working directory */
    if (selector == 2) { s->dirs.wd[-vref - DIRS_WD_BASE] = 0; return 0; }
    mac_cpu_write(cpu, pb + 28, 4, 0);                                /* GetWDInfo: ioWDProcID, ioWDVRefNum (the data volume), ioWDDirID */
    mac_cpu_write(cpu, pb + 32, 2, 0xffff);
    mac_cpu_write(cpu, pb + 48, 4, id);
    return 0;
}
