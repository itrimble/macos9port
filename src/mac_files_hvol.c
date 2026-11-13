#include "mac_runtime.h"
#include "mac_files_int.h"
#include <string.h>

/* PBHGetVol ($A214) and PBHSetVol ($A215), the HFS forms of GetVol/SetVol: the parameter block adds the default directory, ioWDDirID at +48 (ioWDProcID
 * +28 and ioWDVRefNum +32 are zeroed / the volume). The volume is the single mounted volume; its default directory starts as its root, directory ID 2. Get answers the name, volume reference and root ID. Set accepts a volume (by name or reference, as SetVol) and a directory
 * ID (or a working-directory reference) that becomes the default directory, where bare names and partial paths are looked up; an unknown directory is dirNFErr (-120). (From memory of the HFS parameter block layout
 * in Inside Macintosh: Files; the byte offsets match what Glider PRO's glue reads: ioVRefNum +22, ioWDDirID +48.) */
int mac_files_hvol(MacRuntime *rt, uint32_t pb, int get) {
    MacCpu *cpu = rt->cpu;
    FileState *s = rt->file_state;
    if (get) {
        int error = mac_files_get_volume(rt, pb);
        if (error) return error;
        mac_cpu_write(cpu, pb + 28, 4, 0);
        mac_cpu_write(cpu, pb + 32, 2, 0xffff);
        mac_cpu_write(cpu, pb + 48, 4, mac_dirs_id(&s->dirs, s->dirs.current));
        return 0;
    }
    uint32_t dir = mac_cpu_read(cpu, pb + 48, 4);
    int vref = (int16_t)mac_cpu_read(cpu, pb + 22, 2);
    int error = mac_files_set_vol(rt, pb);
    if (error) return error;
    if (!dir) dir = mac_files_wd_folder(rt, vref);
    if (!dir) return 0;   /* the default directory stays as it is */
    char path[DIRS_PATH_MAX];
    if (mac_dirs_path(&s->dirs, dir, path) || !mac_dirs_is_dir(s, path)) return -120;
    memcpy(s->dirs.current, path, sizeof path);
    return 0;
}
