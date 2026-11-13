#include "mac_files_int.h"
#include "mac_resource_chain.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* PBGetFCBInfo (HFSDispatch selector 8), FCBPBRec: ioNamePtr +18, ioVRefNum +22, ioRefNum +24, ioFCBIndx +28 (0: the reference in ioRefNum; n: the n-th open
 * file), ioFCBFlNm +32 (file number), ioFCBFlags +36, ioFCBStBlk +38, ioFCBEOF +40, ioFCBPLen +44, ioFCBCrPs +48 (the mark), ioFCBVRefNum +52, ioFCBClpSiz +54,
 * ioFCBParID +58 (the directory the file is in). Programs use it to find where they live: the application's own resource file (reference 1) is reported as
 * the file it was started from, in the root. A file this service holds open reports the directory of its path. The layout is the one the System volume's
 * implementation (mac_system_volume.c) already uses; the flag bits are not claimed beyond "resource fork" (0x0200) and "writable" (0x0100). */
static GuestFile *fcb_file(FileState *s, int ref) {
    int i = ref - REF_BASE;
    return i >= 0 && i < FILE_LIMIT && s->files[i].fd >= 0 ? &s->files[i] : NULL;
}
static GuestFile *fcb_nth(FileState *s, int index) {
    int seen = 0;
    for (int i = 0; i < FILE_LIMIT; i++) if (s->files[i].fd >= 0 && ++seen == index) return &s->files[i];
    return NULL;
}

int mac_files_fcb_ours(MacRuntime *rt) {
    FileState *s = rt->file_state;
    uint32_t pb = mac_cpu_get(rt->cpu, MAC_A0);
    if (!s || (pb & 1) || !mac_files_valid(rt->cpu, pb, 62)) return 0;
    int ref = (int16_t)mac_cpu_read(rt->cpu, pb + 24, 2), index = (int16_t)mac_cpu_read(rt->cpu, pb + 28, 2);
    if (index > 0) { int vref = (int16_t)mac_cpu_read(rt->cpu, pb + 22, 2); return (vref == 0 || vref == -1) && fcb_nth(s, index) != NULL; }
    return fcb_file(s, ref) != NULL || (ref == MAC_APP_RES_REF && s->app_name[0] != 0);
}

void mac_files_set_app(MacRuntime *rt, const char *name) {
    FileState *s = rt ? rt->file_state : NULL;
    if (s && name) snprintf(s->app_name, sizeof s->app_name, "%s", name);
}

int mac_files_fcb_info(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (!s) return -35;
    int ref = (int16_t)mac_cpu_read(cpu, pb + 24, 2), index = (int16_t)mac_cpu_read(cpu, pb + 28, 2);
    if (index < 0) return -51;
    GuestFile *f = index > 0 ? fcb_nth(s, index) : fcb_file(s, ref);
    int app = !f && ref == MAC_APP_RES_REF && index == 0 && s->app_name[0];
    if (!f && !app) return -51;
    if (index > 0) ref = REF_BASE + (int)(f - s->files);
    const char *path = f ? f->name : s->app_name;
    char parent[DIRS_PATH_MAX]; const char *leaf;
    mac_dirs_parent(path, parent, &leaf);
    uint32_t name_ptr = mac_cpu_read(cpu, pb + 18, 4);
    if (name_ptr && !mac_files_valid(cpu, name_ptr, FILE_NAME_MAX + 1)) return -50;
    struct stat st; memset(&st, 0, sizeof st);
    if (f && fstat(f->fd, &st)) return -36;
    if (app && !mac_dirs_path_ok(path)) memset(&st, 0, sizeof st);
    else if (app) mac_dirs_stat(s->directory, path, &st);   /* the data fork may be absent: the sizes stay 0 */
    uint32_t eof = (uint32_t)st.st_size;
    mac_cpu_write(cpu, pb + 24, 2, (uint16_t)ref);
    mac_cpu_write(cpu, pb + 32, 4, (uint32_t)st.st_ino ? (uint32_t)st.st_ino : 16u);
    mac_cpu_write(cpu, pb + 36, 2, (uint16_t)(0x6000 | (app ? 0x0200 : 0) | (f && f->writable ? 0x0100 : 0)));
    mac_cpu_write(cpu, pb + 38, 2, 0);
    mac_cpu_write(cpu, pb + 40, 4, eof);
    mac_cpu_write(cpu, pb + 44, 4, (eof + 511u) & ~511u);
    mac_cpu_write(cpu, pb + 48, 4, f ? (uint32_t)f->mark : 0);
    mac_cpu_write(cpu, pb + 52, 2, 0xffff);
    mac_cpu_write(cpu, pb + 54, 4, 512);
    mac_cpu_write(cpu, pb + 58, 4, mac_dirs_id(&s->dirs, parent));
    if (name_ptr) {
        size_t n = strlen(leaf) > FILE_NAME_MAX ? FILE_NAME_MAX : strlen(leaf);
        mac_cpu_ram(cpu)[name_ptr] = (uint8_t)n;
        for (size_t i = 0; i < n; i++) { unsigned char c = (unsigned char)leaf[i]; mac_cpu_ram(cpu)[name_ptr + 1 + i] = c >= 32 && c < 127 ? c : '?'; }
    }
    return 0;
}
