#include "mac_files_int.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Create, delete and rename on the writable save overlay (the cores shared by the PB calls and the FSSpec calls), split from mac_files_write.c. */
/* The *_name cores are shared by the PB calls (parameter-block names) and the FSSpec calls. */
int mac_files_create_name(MacRuntime *rt, const char *name) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    if (s->saves < 0) return -46;
    if (!mac_files_save_name_ok(name)) return -50;
    if (mac_files_classify(s, name) != FILE_NONE) return -48;
    unsigned files; uint64_t bytes;
    int error = mac_files_usage(s, &files, &bytes);
    if (error) return error;
    if (files >= s->max_files) return -34;
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(name, parent, &leaf);
    if (parent[0] && (error = mac_dirs_ensure_overlay(s, parent))) return error;
    int fd = mac_dirs_openat(s->saves, name, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return errno == EEXIST ? -48 : errno == ENOENT || errno == ENOTDIR ? -120 : -36;
    close(fd);
    fsync(s->saves);
    return 0;
}

int mac_files_create(MacRuntime *rt, uint32_t pb) {
    char name[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(rt->cpu, pb + 18, 4), name, 0);
    if (error) return error;
    if ((error = mac_files_volume_ok_rt(rt, pb))) return error;
    return mac_files_create_name(rt, name);
}

/* Delete an empty directory of the overlay. A directory that holds anything (here or in the application data) is fBsyErr; one that only the read-only
 * application data has is locked. */
static int delete_directory(FileState *s, const char *name) {
    if (s->saves < 0) return -46;
    struct stat st;
    if (mac_dirs_stat(s->saves, name, &st) || !S_ISDIR(st.st_mode)) return -45;
    char (*names)[256]; unsigned count;
    int error = mac_files_listing_dir(s, name, 1, 0, &names, &count);
    if (error) return error;
    free(names);
    if (count) return -47;
    if (mac_dirs_rmdir(s->saves, name)) return errno == ENOENT ? -43 : -36;
    fsync(s->saves);
    return 0;
}

int mac_files_delete_name(MacRuntime *rt, const char *name) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    if (!mac_files_save_name_ok(name)) return -50;
    if (mac_dirs_is_dir(s, name)) return delete_directory(s, name);
    int error = mac_files_locked_error(s, mac_files_classify(s, name));
    if (error) return error;
    if (mac_files_open_by_name(s, name)) return -47;
    if (mac_dirs_unlink(s->saves, name)) return errno == ENOENT ? -43 : -36;
    fsync(s->saves);
    return 0;
}

int mac_files_delete(MacRuntime *rt, uint32_t pb, int hfs) {
    (void)hfs;
    char name[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(rt->cpu, pb + 18, 4), name, 0);
    if (error) return error;
    if ((error = mac_files_volume_ok_rt(rt, pb))) return error;
    return mac_files_delete_name(rt, name);
}

/* Rename within a directory, or move (rename across directories) of a save file or an overlay directory. */
int mac_files_rename_names(MacRuntime *rt, const char *from, const char *to) {
    FileState *s = rt->file_state;
    int dir = mac_dirs_is_dir(s, from);
    if (dir) {
        struct stat st;
        if (s->saves < 0) return -46;
        if (mac_dirs_stat(s->saves, from, &st) || !S_ISDIR(st.st_mode)) return -45;
        size_t n = strlen(from);
        if (!strncmp(to, from, n) && to[n] == '/') return -48;   /* a directory cannot move into itself */
    } else {
        int error = mac_files_locked_error(s, mac_files_classify(s, from));
        if (error) return error;
        if (mac_files_open_by_name(s, from)) return -47;
    }
    if (!mac_files_save_name_ok(to)) return -50;
    if (mac_files_classify(s, to) != FILE_NONE || mac_dirs_is_dir(s, to)) {
        /* The name is taken, unless `to` is the very same entry spelled in another case (HFS names ignore case): then it is a case-only rename. */
        struct stat a, b;
        int same = !mac_dirs_stat(s->saves, from, &a) && !mac_dirs_stat(s->saves, to, &b) && a.st_dev == b.st_dev && a.st_ino == b.st_ino &&
                   mr_host_equal_fold(from, to);
        if (!same) return -48;
    }
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(to, parent, &leaf);
    int error = parent[0] ? mac_dirs_ensure_overlay(s, parent) : 0;
    if (error) return error;
    if (mac_dirs_rename(s->saves, from, to)) return errno == ENOENT ? -43 : -36;
    fsync(s->saves);
    return 0;
}

/* PBRename / PBHRename: the new name (ioMisc, +28) is a name in the same directory. */
int mac_files_rename(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    char from[DIRS_PATH_MAX], to[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(cpu, pb + 18, 4), from, 0);
    if (error) return error;
    if ((error = mac_files_volume_ok_rt(rt, pb))) return error;
    uint32_t new_ptr = mac_cpu_read(cpu, pb + 28, 4);
    if (!new_ptr || !mac_files_valid(cpu, new_ptr, 1)) return -50;
    unsigned n = mac_cpu_read(cpu, new_ptr, 1);
    if (!n || n > FILE_NAME_MAX || !mac_files_valid(cpu, new_ptr + 1, n)) return -50;
    char leaf_name[MR_HOST_MAX];
    /* the new name is a bare Mac Roman name (no ':' or '/'), kept in exactly the spelling given so a case-only rename works */
    if (mr_component_to_host(mac_cpu_ram(cpu) + new_ptr + 1, n, leaf_name, sizeof leaf_name) < 0) return -50;
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(from, parent, &leaf);
    if (snprintf(to, sizeof to, "%s%s%s", parent, parent[0] ? "/" : "", leaf_name) >= (int)sizeof to) return -37;
    if (!strcmp(from, to)) return 0;
    return mac_files_rename_names(rt, from, to);
}

