#include "mac_runtime.h"
#include "mac_files_int.h"
#include <stdio.h>
#include <string.h>

/* PBOpenRF ($A00A), PBHOpenRF ($A20A) and FSpOpenRF: open a file's resource fork. Parameter block as for PBOpen: ioNamePtr +18, ioVRefNum +22, ioRefNum +24 (result),
 * ioPermssn +27 (the HFS form also carries ioDirID +48). The model: the resource fork of the file "X" is the byte file "X.rsrc" next to it, in the same
 * directory (the same sidecar the application volume's read-only exports use). On the writable save overlay the sidecar is created empty the first time
 * its fork is opened, like a new file's empty fork, and then behaves as an ordinary file: reads, writes, EOF, close, the one-writer rule, the staging copy
 * and the quota are the data fork's. On the application volume only the existing export can be opened, read-only. Names longer than 26 characters cannot
 * carry a fork (the ".rsrc" suffix would pass the 31-character limit): paramErr. Deleting or renaming a file does not yet move its sidecar.
 * Asynchronous calls are refused by the dispatcher. */
int mac_files_open_rf_path(MacRuntime *rt, const char *name, unsigned permission, int *ref) {
    const char *leaf = strrchr(name, '/');
    leaf = leaf ? leaf + 1 : name;
    if (strlen(leaf) + 5 > FILE_NAME_MAX || strlen(name) + 6 > DIRS_PATH_MAX) return -50;
    char sidecar[DIRS_PATH_MAX];
    snprintf(sidecar, sizeof sidecar, "%s.rsrc", name);
    FileState *s = rt->file_state;
    int kind = mac_files_classify(s, name);
    if (kind == FILE_NONE) return -43;
    if (kind != FILE_SAVE && kind != FILE_DATA) return -54;
    int error;
    if (kind == FILE_SAVE && mac_files_classify(s, sidecar) == FILE_NONE) {
        if ((error = mac_files_create_name(rt, sidecar))) return error;
    }
    return mac_files_open_name(rt, sidecar, permission, ref);
}

int mac_files_open_rf(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    int volume = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    unsigned permission = mac_cpu_read(cpu, pb + 27, 1);
    char name[DIRS_PATH_MAX];
    if (!rt->file_state || (volume != 0 && volume != -1 && mac_files_volume_ok_rt(rt, pb))) return -35;
    int error = mac_files_pb_path(rt, pb, mac_cpu_read(cpu, pb + 18, 4), name, 0);
    if (error) return error;
    int ref;
    if ((error = mac_files_open_rf_path(rt, name, permission, &ref))) return error;
    mac_cpu_write(cpu, pb + 24, 2, (uint16_t)ref);
    return 0;
}
