#include "mac_files_int.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* PBSetCatInfo (HFSDispatch selector 10), from the public Files chapter in my own words. CInfoPBRec offsets as in mac_catinfo.c: ioNamePtr +18, ioVRefNum +22,
 * Finder info +32 (16 bytes, a file's FInfo or a folder's DInfo), ioDirID +48. The item is the one ioNamePtr names in ioDirID, or the directory ioDirID itself
 * when ioNamePtr is nil. The call stores the 16-byte Finder info (a folder's keeps its position and flags, a file's its type and creator) in the host
 * metadata of the item in the save overlay, where PBGetCatInfo reads it back. Dates, the lock attribute and the extended Finder info of the block are accepted
 * and not applied. A file of the read-only application data is fLckdErr as for PBSetFInfo; a folder that exists only there is first made in the overlay (the
 * folder is not a file, nothing is copied). No writable overlay is -46. */
int mac_files_set_cat_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    s->hfs_call = 1;
    mac_files_flush_staged(s);
    if (!mac_files_valid(cpu, pb, 104)) return -50;
    if (s->saves < 0) return -46;
    char path[DIRS_PATH_MAX];
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(cpu, pb + 18, 4), path, 1);
    if (error) return error;
    int fd;
    if (mac_dirs_is_dir(s, path)) {
        if (!path[0]) return 0;   /* the volume's own Finder info is not kept */
        if ((error = mac_dirs_ensure_overlay(s, path))) return error;
        fd = mac_dirs_openat(s->saves, path, O_RDONLY, 0);
        if (fd < 0) return -36;
    } else {
        if ((error = mac_files_locked_error(s, mac_files_classify(s, path)))) return error;
        fd = mac_files_open_regular(s->saves, path, O_RDWR);
        if (fd < 0) return fd;
    }
    error = mac_files_finder_set(fd, mac_cpu_ram(cpu) + pb + 32);
    close(fd);
    return error;
}
