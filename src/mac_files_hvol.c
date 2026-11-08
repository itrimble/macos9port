#include "mac_runtime.h"
#include "mac_files_int.h"

/* PBHGetVol ($A214) and PBHSetVol ($A215), the HFS forms of GetVol/SetVol: the parameter block adds the default directory, ioWDDirID at +48 (ioWDProcID
 * +28 and ioWDVRefNum +32 are zeroed / the volume). The volume is the single mounted volume; its default directory is its root, directory ID 2 (the same
 * root the catalog calls report). Get answers the name, volume reference and root ID. Set accepts a volume (by name or reference, as SetVol) with
 * directory ID 0 (the default) or 2; any other directory does not exist on this flat volume: dirNFErr (-120). (From memory of the HFS parameter block layout
 * in Inside Macintosh: Files; the byte offsets match what Glider PRO's glue reads: ioVRefNum +22, ioWDDirID +48.) */
int mac_files_hvol(MacRuntime *rt, uint32_t pb, int get) {
    MacCpu *cpu = rt->cpu;
    if (get) {
        int error = mac_files_get_volume(rt, pb);
        if (error) return error;
        mac_cpu_write(cpu, pb + 28, 4, 0);
        mac_cpu_write(cpu, pb + 32, 2, 0xffff);
        mac_cpu_write(cpu, pb + 48, 4, 2);
        return 0;
    }
    uint32_t dir = mac_cpu_read(cpu, pb + 48, 4);
    if (dir != 0 && dir != 2) return -120;
    return mac_files_set_vol(rt, pb);
}
