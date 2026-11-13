#include "mac_runtime.h"
#include "mac_files_int.h"

#include <stdio.h>
#include <string.h>

/* A one-process Process Manager: OSDispatch ($A88F) with the selector on the stack, as the public Processes interface lays it out. The program is the only
 * process, so every question about processes has the same answer: its serial number is the same everywhere, it is in front, and there is no next process.
 * Selectors (public interface): 37 GetCurrentProcess, 38 GetNextProcess, 39 GetFrontProcess, 3A GetProcessInformation, 3B SetFrontProcess, 3C WakeUpProcess,
 * 3D SameProcess.
 * Anything else halts with its selector named; nothing is guessed.
 * Serial numbers 0, 1 and 2 are the special values kNoProcess, kSystemProcess, kCurrentProcess; ours is {0, 0x00010002}. */
#define PSN_LOW UINT32_C(0x00010002)
enum { PM_NO_ERR = 0, PM_PROC_NOT_FOUND = -600, PM_PARAM_ERR = -50 };

static int ok(MacRuntime *rt, uint32_t sp, unsigned bytes) {
    uint32_t z = (uint32_t)mac_cpu_ram_size(rt->cpu);
    if ((sp & 1) || sp < 0x400 || sp > z || z - sp < bytes) { mac_cpu_halt(rt->cpu, "Process Manager stack is invalid."); return 0; }
    return 1;
}
static int valid_ptr(MacRuntime *rt, uint32_t p, unsigned n) {
    return p && p <= mac_cpu_ram_size(rt->cpu) && n <= mac_cpu_ram_size(rt->cpu) - p;
}
static int is_ours(uint32_t high, uint32_t low) { return high == 0 && (low == PSN_LOW || low == 2); }

/* pops `args` bytes (selector included), stores the OSErr result word above them */
static int finish(MacRuntime *rt, uint32_t sp, unsigned args, int error) {
    mac_cpu_write(rt->cpu, sp + args, 2, (uint16_t)error);
    mac_cpu_set(rt->cpu, MAC_A7, sp + args);
    return mac_cpu_halted(rt->cpu) ? -1 : 1;
}

int mac_process_service(MacRuntime *rt, uint16_t opcode) {
    if (opcode != 0xa88f) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    uint32_t sp = mac_cpu_get(rt->cpu, MAC_A7);
    if (!ok(rt, sp, 4)) return -1;
    unsigned selector = mac_cpu_read(rt->cpu, sp, 2) & 0xffff;
    switch (selector) {
    case 0x37: case 0x39: case 0x38: {   /* GetCurrentProcess, GetFrontProcess, GetNextProcess(var psn) */
        if (!ok(rt, sp, 8)) return -1;
        uint32_t psn = mac_cpu_read(rt->cpu, sp + 2, 4);
        if (!valid_ptr(rt, psn, 8)) return finish(rt, sp, 6, PM_PARAM_ERR);
        if (selector == 0x38) {   /* the first call starts from kNoProcess and gets ours; the next gets procNotFound */
            if (mac_cpu_read(rt->cpu, psn, 4) == 0 && mac_cpu_read(rt->cpu, psn + 4, 4) == 0) {
                mac_cpu_write(rt->cpu, psn + 4, 4, PSN_LOW);
                return finish(rt, sp, 6, PM_NO_ERR);
            }
            mac_cpu_write(rt->cpu, psn, 4, 0); mac_cpu_write(rt->cpu, psn + 4, 4, 0);
            return finish(rt, sp, 6, PM_PROC_NOT_FOUND);
        }
        mac_cpu_write(rt->cpu, psn, 4, 0); mac_cpu_write(rt->cpu, psn + 4, 4, PSN_LOW);
        return finish(rt, sp, 6, PM_NO_ERR);
    }
    case 0x3b: case 0x3c: {              /* SetFrontProcess, WakeUpProcess(psn*) */
        if (!ok(rt, sp, 8)) return -1;
        uint32_t psn = mac_cpu_read(rt->cpu, sp + 2, 4);
        if (!valid_ptr(rt, psn, 8)) return finish(rt, sp, 6, PM_PARAM_ERR);
        return finish(rt, sp, 6, is_ours(mac_cpu_read(rt->cpu, psn, 4), mac_cpu_read(rt->cpu, psn + 4, 4)) ? PM_NO_ERR : PM_PROC_NOT_FOUND);
    }
    case 0x3a: {                         /* GetProcessInformation(psn*, var info: ProcessInfoRec) */
        if (!ok(rt, sp, 14)) return -1;
        uint32_t info = mac_cpu_read(rt->cpu, sp + 2, 4), psn = mac_cpu_read(rt->cpu, sp + 6, 4);
        if (!valid_ptr(rt, psn, 8) || !valid_ptr(rt, info, 4)) return finish(rt, sp, 10, PM_PARAM_ERR);
        if (!is_ours(mac_cpu_read(rt->cpu, psn, 4), mac_cpu_read(rt->cpu, psn + 4, 4))) return finish(rt, sp, 10, PM_PROC_NOT_FOUND);
        uint32_t len = mac_cpu_read(rt->cpu, info, 4);
        if (len < 16 || len > 256 || !valid_ptr(rt, info, len)) return finish(rt, sp, 10, PM_PARAM_ERR);
        /* Record layout (public): 0 length, 4 name ptr (Str31 buffer), 8 serial number, 16 type, 20 signature, 24 mode, 28 location, 32 size,
         * 36 free memory, 40 launcher serial number, 48 launch date, 52 active time, 56 FSSpec ptr. Only what is known is written. */
        const char *name = rt->file_state ? ((FileState *)rt->file_state)->app_name : "";
        uint32_t name_ptr = len >= 8 ? mac_cpu_read(rt->cpu, info + 4, 4) : 0, spec_ptr = len >= 60 ? mac_cpu_read(rt->cpu, info + 56, 4) : 0;
        size_t n = strlen(name) > 31 ? 31 : strlen(name);
        if (name_ptr && valid_ptr(rt, name_ptr, 32)) {
            mac_cpu_write(rt->cpu, name_ptr, 1, (uint32_t)n);
            for (size_t i = 0; i < n; i++) mac_cpu_write(rt->cpu, name_ptr + 1 + (uint32_t)i, 1, (unsigned char)name[i]);
        }
        if (len >= 16) { mac_cpu_write(rt->cpu, info + 8, 4, 0); mac_cpu_write(rt->cpu, info + 12, 4, PSN_LOW); }
        if (len >= 24) { mac_cpu_write(rt->cpu, info + 16, 4, 0x4150504c); mac_cpu_write(rt->cpu, info + 20, 4, 0x3f3f3f3f); }   /* 'APPL', '????' */
        if (len >= 32) mac_cpu_write(rt->cpu, info + 24, 4, 0);   /* mode, location, size and memory figures are not reported: the fields stay as the caller left them */
        if (spec_ptr && valid_ptr(rt, spec_ptr, 70) && n) {   /* FSSpec: vRefNum (the data volume), parent directory ID (root), name */
            mac_cpu_write(rt->cpu, spec_ptr, 2, 0xffff); mac_cpu_write(rt->cpu, spec_ptr + 2, 4, 2);
            mac_cpu_write(rt->cpu, spec_ptr + 6, 1, (uint32_t)n);
            for (size_t i = 0; i < n; i++) mac_cpu_write(rt->cpu, spec_ptr + 7 + (uint32_t)i, 1, (unsigned char)name[i]);
        }
        return finish(rt, sp, 10, PM_NO_ERR);
    }
    case 0x3d: {                         /* SameProcess(psn1*, psn2*, var same: Boolean) */
        if (!ok(rt, sp, 16)) return -1;
        uint32_t same = mac_cpu_read(rt->cpu, sp + 2, 4), b = mac_cpu_read(rt->cpu, sp + 6, 4), a = mac_cpu_read(rt->cpu, sp + 10, 4);
        if (!valid_ptr(rt, a, 8) || !valid_ptr(rt, b, 8) || !valid_ptr(rt, same, 1)) return finish(rt, sp, 14, PM_PARAM_ERR);
        uint32_t ah = mac_cpu_read(rt->cpu, a, 4), al = mac_cpu_read(rt->cpu, a + 4, 4), bh = mac_cpu_read(rt->cpu, b, 4), bl = mac_cpu_read(rt->cpu, b + 4, 4);
        int eq = (ah == bh && al == bl) || (is_ours(ah, al) && is_ours(bh, bl));
        mac_cpu_write(rt->cpu, same, 1, eq ? 1 : 0);
        return finish(rt, sp, 14, PM_NO_ERR);
    }
    default: {
        char msg[120];
        snprintf(msg, sizeof msg, "Process Manager selector $%02X (OSDispatch $A88F) is not implemented.", selector);
        mac_cpu_halt(rt->cpu, msg);
        return -1;
    }
    }
}
