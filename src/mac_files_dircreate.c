#include "mac_files_int.h"
#include <stdio.h>
#include <string.h>

/* PBDirCreate (HFSDispatch selector 6) on the approximated folder model of mac_fsspec.c: folders are not real directories. A new folder gets an ID from 3 to 66
 * derived from its name (the same on every launch, no table), and nothing is created on the host; files whose parent is such an ID are stored flat as "d<id>-name".
 * Parameter block: ioNamePtr +18 (the folder's name), ioVRefNum +22, ioDirID +48 (the parent: 0 or the root, 2; folders do not nest), result ioDirID +48.
 * Errors: -46 no writable save overlay, -35 unknown volume, -120 a parent that is not the root, -37 a bad name, -50 a bad pointer, -48 a file holds the name. */
uint32_t mac_files_folder_id(const char *name) {
    uint32_t h = 2166136261u;
    for (const char *c = name; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
    return FOLDER_ID_FIRST + h % (FOLDER_ID_LAST - FOLDER_ID_FIRST + 1);
}

/* The folder named `name` in the root: 0 and *id when DirCreate made it earlier; otherwise fnfErr (-43), or, with `create`, it is made now (-46 without a writable
 * save overlay, like DirCreate). Used by FindFolder (mac_alias.c). */
int mac_files_folder_find(MacRuntime *rt, const char *name, int create, uint32_t *id) {
    FileState *s = rt->file_state;
    if (!s) return -35;
    *id = mac_files_folder_id(name);
    char marker[FILE_NAME_MAX + 1]; snprintf(marker, sizeof marker, "d%u-.f", *id);
    if (s->saves >= 0 && mac_files_classify(s, marker) != FILE_NONE) return 0;
    if (!create) return -43;
    if (s->saves < 0) return -46;
    if (mac_files_classify(s, name) != FILE_NONE) return -48;
    int made = mac_files_create_name(rt, marker);
    return made == -48 ? 0 : made;
}

int mac_files_dir_create(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s || s->saves < 0) return -46;
    int vref = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    if (!(vref == 0 || vref == -1 || (vref == -2 && rt->system_volume))) return -35;
    uint32_t parent = mac_cpu_read(cpu, pb + 48, 4);
    if (parent != 0 && parent != 2) return -120;
    uint32_t ptr = mac_cpu_read(cpu, pb + 18, 4);
    if (!ptr || !mac_files_valid(cpu, ptr, 1)) return -50;
    unsigned n = mac_cpu_read(cpu, ptr, 1);
    if (!mac_files_valid(cpu, ptr + 1, n)) return -50;
    if (!n || n > FILE_NAME_MAX) return -37;
    char name[FILE_NAME_MAX + 1];
    memcpy(name, mac_cpu_ram(cpu) + ptr + 1, n); name[n] = 0;
    int high = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32 || c == 127 || c == 255 || c == ':' || c == '/' || c == '\\') return -37;
        if (c >= 128) high = 1;
    }
    if (!high && mac_files_classify(s, name) != FILE_NONE) return -48;   /* a file holds the name */
    uint32_t id = mac_files_folder_id(name);
    char marker[FILE_NAME_MAX + 1]; snprintf(marker, sizeof marker, "d%u-.f", id);       /* a folder exists once its marker file does (hidden from listings like every folder file) */
    int made = mac_files_create_name(rt, marker);
    if (made && made != -48) return made;                                                /* -48: already there, which is fine */
    mac_cpu_write(cpu, pb + 48, 4, id);
    return 0;
}

/* PBGetCatInfo by name (ioFDirIndex 0) of a folder made by DirCreate: a directory record. Returns 1 and fills the block when `name` has a marker and no file of that
 * name exists; 0 otherwise (the caller then looks for a file). Parent 0 or the root only. */
int mac_files_folder_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    uint32_t parent = mac_cpu_read(cpu, pb + 48, 4), ptr = mac_cpu_read(cpu, pb + 18, 4);
    if (!s || s->saves < 0 || (parent != 0 && parent != 2) || !ptr || !mac_files_valid(cpu, ptr, 1)) return 0;
    unsigned n = mac_cpu_read(cpu, ptr, 1);
    if (!n || n > FILE_NAME_MAX || !mac_files_valid(cpu, ptr + 1, n)) return 0;
    char name[FILE_NAME_MAX + 1];
    memcpy(name, mac_cpu_ram(cpu) + ptr + 1, n); name[n] = 0;
    for (unsigned i = 0; i < n; i++) if ((unsigned char)name[i] < 32 || name[i] == ':' || name[i] == '/' || name[i] == '\\') return 0;
    int high = 0; for (unsigned i = 0; i < n; i++) if ((unsigned char)name[i] >= 128) high = 1;

    if (!high && mac_files_classify(s, name) != FILE_NONE) return 0;                    /* a file of that name wins */
    uint32_t id = mac_files_folder_id(name);
    char marker[FILE_NAME_MAX + 1]; snprintf(marker, sizeof marker, "d%u-.f", id);
    if (mac_files_classify(s, marker) == FILE_NONE) return 0;
    if (!mac_files_valid(cpu, pb, 104)) return 0;
    mac_cpu_write(cpu, pb + 30, 1, 0x10);                                                /* ioFlAttrib: a directory */
    memset(mac_cpu_ram(cpu) + pb + 31, 0, 17);
    mac_cpu_write(cpu, pb + 48, 4, id);                                                  /* ioDrDirID */
    mac_cpu_write(cpu, pb + 52, 2, 0);                                                   /* ioDrNmFls: files are not counted per folder */
    memset(mac_cpu_ram(cpu) + pb + 72, 0, 16);
    mac_cpu_write(cpu, pb + 100, 4, 2);                                                  /* ioDrParID: the root */
    return 1;
}

int mac_files_volume_ok_rt(MacRuntime *rt, uint32_t pb) {
    int vref = (int16_t)mac_cpu_read(rt->cpu, pb + 22, 2);
    if ((vref == -2 && rt->system_volume) || mac_files_wd_folder(vref)) return 0;
    return mac_files_volume_ok(rt->cpu, pb);
}

/* Working directories for made folders (HFSDispatch selectors 1 PBOpenWD, 2 PBCloseWD, 7 PBGetWDInfo): the WD reference of folder `id` is -(100 + id), so it needs no table and
 * is the same every launch. Only folders with a marker (DirCreate made them) can be opened. The reference is accepted as ioVRefNum by FSMakeFSSpec (the name is then in that
 * folder); the PB file calls do not take it yet (they see an unknown volume, -35). */
uint32_t mac_files_wd_folder(int vref) {
    int64_t id = -(int64_t)vref - 100;
    return id >= FOLDER_ID_FIRST && id <= FOLDER_ID_LAST ? (uint32_t)id : 0;
}
static int folder_made(FileState *s, uint32_t id) {
    char marker[FILE_NAME_MAX + 1]; snprintf(marker, sizeof marker, "d%u-.f", id);
    return s && s->saves >= 0 && mac_files_classify(s, marker) != FILE_NONE;
}
/* Whether PBOpenWD / PBGetWDInfo names one of ours (so the System volume's own handler leaves it alone): by dirID for selector 1, by reference for 2 and 7. */
int mac_files_wd_ours(const MacRuntime *rt, unsigned selector, uint32_t dir_id, int vref) {
    (void)rt;
    if (selector == 1) return dir_id >= FOLDER_ID_FIRST && dir_id <= FOLDER_ID_LAST;
    return (selector == 2 || selector == 7) && mac_files_wd_folder(vref) != 0;
}
int mac_files_wd(MacRuntime *rt, uint32_t pb, unsigned selector) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    int vref = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    if (selector == 1) {
        uint32_t id = mac_cpu_read(cpu, pb + 48, 4);
        if (!(vref == 0 || vref == -1 || (vref == -2 && rt->system_volume))) return -35;
        if (id < FOLDER_ID_FIRST || id > FOLDER_ID_LAST || !folder_made(s, id)) return -120;
        mac_cpu_write(cpu, pb + 22, 2, (uint16_t)(-(int)(100 + id)));
        return 0;
    }
    uint32_t id = mac_files_wd_folder(vref);
    if (!id || !folder_made(s, id)) return -51;                       /* rfNumErr: not an open working directory */
    if (selector == 2) return 0;
    mac_cpu_write(cpu, pb + 28, 4, 0);                                /* GetWDInfo: ioWDProcID, ioWDVRefNum (the data volume), ioWDDirID */
    mac_cpu_write(cpu, pb + 32, 2, 0xffff);
    mac_cpu_write(cpu, pb + 48, 4, id);
    return 0;
}

/* A name from a parameter block's ioNamePtr, in the flat host namespace: when ioVRefNum is a working directory of a made folder the name is that folder's file, "d<id>-name". */
int mac_files_pb_name(MacRuntime *rt, uint32_t pb, uint32_t name_ptr, char *out, size_t capacity) {
    int error = mac_files_read_name(rt->cpu, name_ptr, out, capacity);
    if (error) return error;
    uint32_t id = mac_files_wd_folder((int16_t)mac_cpu_read(rt->cpu, pb + 22, 2));
    if (!id) return 0;
    if (!folder_made(rt->file_state, id)) return -35;
    char tmp[256]; int k = snprintf(tmp, sizeof tmp, "d%u-%s", id, out);
    if (k < 0 || (size_t)k >= capacity || (size_t)k >= sizeof tmp) return -37;
    memcpy(out, tmp, (size_t)k + 1);
    return 0;
}
