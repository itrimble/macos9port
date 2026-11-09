#include "mac_files_int.h"
#include <string.h>

/* PBHGetVolParms (HFSDispatch selector $30; layout from memory of Inside Macintosh: Files): fills the caller's buffer with the GetVolParmsInfoBuffer of the one volume
 * and sets ioActCount (offset 40) to the bytes returned (at most ioReqCount, offset 36; the buffer pointer is ioBuffer, offset 32). The volume is a local, simple one:
 * version 2, no optional capability bits (no deny-mode opens, no desktop database, file IDs or catalog search), no server, no local handle, grade 0, foreign
 * privilege model 0. ioVRefNum must name the data volume (0 or -1 here). Bounds are checked before any byte is written. */
int mac_files_get_volparms(MacRuntime *rt, uint32_t pb) {
    MacCpu *cpu = rt->cpu;
    if (!rt->file_state) return -35;
    int error = (int16_t)mac_cpu_read(cpu, pb + 22, 2) == -2 ? 0 : mac_files_volume_ok(cpu, pb);   /* the data volume (0, -1) or the system volume (-2, which Glider asks about): both local */
    if (error) return error;
    uint32_t buffer = mac_cpu_read(cpu, pb + 32, 4);
    int32_t request = (int32_t)mac_cpu_read(cpu, pb + 36, 4);
    if (request < 0 || request > 0x10000) return -50;
    uint8_t info[20] = {0};
    info[1] = 2;                                      /* vMVersion */
    size_t n = (size_t)request < sizeof info ? (size_t)request : sizeof info;
    if (n && (!buffer || !mac_files_valid(cpu, buffer, (uint32_t)n))) return -50;
    memcpy(mac_cpu_ram(cpu) + buffer, info, n);
    mac_cpu_write(cpu, pb + 40, 4, (uint32_t)n);
    return 0;
}
