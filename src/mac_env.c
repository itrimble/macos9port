#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

/* Services INITs call while a system starts: the zone globals, Notification
 * Manager, Delay, ReadDateTime, driver install/remove, ADB probing and a few
 * Resource Manager calls. Written from the documented register and Pascal
 * calling conventions. Anything with no meaningful effect in a headless guest
 * (Notification Manager posts, ADB devices, driver installs) reports success
 * without doing it; each such case is named in a comment. */

enum { THE_ZONE = 0x118, SYS_ZONE = 0x2a6, APPL_ZONE = 0x2aa, RES_ERR = 0xa60, MEM_ERR = 0x220,
       MAC_EPOCH_DELTA = 2082844800u };

static void set_err(MacCpu *cpu, int16_t err) {
    mac_cpu_write(cpu, MEM_ERR, 2, (uint16_t)err);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)err);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (err < 0 ? 8u : err == 0 ? 4u : 0u));
}

static void pop(MacCpu *cpu, uint32_t bytes) { mac_cpu_set(cpu, MAC_A7, mac_cpu_get(cpu, MAC_A7) + bytes); }

/* One authored zone header per runtime: SysZone, ApplZone and TheZone share it. */
static uint32_t zone(MacRuntime *rt) {
    uint32_t z = mac_cpu_read(rt->cpu, APPL_ZONE, 4);
    if (z) return z;
    z = mac_runtime_alloc(rt, 64);
    if (!z) return 0;
    mac_cpu_write(rt->cpu, z, 4, rt->heap_limit);       /* bkLim */
    mac_cpu_write(rt->cpu, z + 12, 4, rt->heap_limit - rt->heap_next); /* zcbFree */
    mac_cpu_write(rt->cpu, APPL_ZONE, 4, z);
    mac_cpu_write(rt->cpu, SYS_ZONE, 4, z);
    mac_cpu_write(rt->cpu, THE_ZONE, 4, z);
    return z;
}

static MacHandle *by_pointer(MacRuntime *rt, uint32_t p) {
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].handle && rt->handles[i].pointer == p && p) return &rt->handles[i];
    return NULL;
}

static int find_named(MacRuntime *rt, uint32_t type, uint32_t name_ptr, int16_t *id) {
    uint8_t len = (uint8_t)mac_cpu_read(rt->cpu, name_ptr, 1);
    for (size_t i = 0; i < mac_resource_count(&rt->fork); i++) {
        MacResource r;
        if (mac_resource_at(&rt->fork, i, &r) || r.type != type || !r.name || r.name_size != len) continue;
        uint8_t want[255];
        for (unsigned k = 0; k < len; k++) want[k] = (uint8_t)mac_cpu_read(rt->cpu, name_ptr + 1 + k, 1);
        if (!memcmp(want, r.name, len)) { *id = r.id; return 0; }
    }
    return -1;
}

static MacHandle *by_handle(MacRuntime *rt, uint32_t h) {
    for (size_t i = 0; i < rt->handle_count; i++)
        if (rt->handles[i].handle == h && h) return &rt->handles[i];
    return NULL;
}

static void put_result16(MacCpu *cpu, uint16_t v) { mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 2, v); }
static void put_result32(MacCpu *cpu, uint32_t v) { mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 4, v); }

static int os_trap(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t a0 = mac_cpu_get(cpu, MAC_A0), d0 = mac_cpu_get(cpu, MAC_D0);
    switch (opcode & 0xff) {
    case 0x1a: mac_cpu_set(cpu, MAC_A0, zone(rt) ? mac_cpu_read(cpu, THE_ZONE, 4) : 0); set_err(cpu, 0); return 1;
    case 0x1b: zone(rt); mac_cpu_write(cpu, THE_ZONE, 4, a0); set_err(cpu, 0); return 1;
    case 0x28: { /* RecoverHandle */
        MacHandle *h = by_pointer(rt, a0);
        mac_cpu_set(cpu, MAC_A0, h ? h->handle : 0);
        set_err(cpu, 0);
        return 1;
    }
    case 0x36: set_err(cpu, 0); return 1; /* MoreMasters: master pointers are never exhausted here */
    case 0x20: { /* SetPtrSize: shrink freely, grow only a trailing block */
        MacHandle *h = NULL;
        for (size_t i = 0; i < rt->handle_count; i++)
            if (!rt->handles[i].handle && rt->handles[i].pointer == a0 && a0) h = &rt->handles[i];
        if (!h) { set_err(cpu, -109); return 1; }
        if (d0 <= h->size) h->size = d0;
        else if (h->pointer + ((h->size + 3) & ~3u) == rt->heap_next && mac_runtime_alloc(rt, d0 - h->size)) h->size = d0;
        else { set_err(cpu, -108); return 1; }
        set_err(cpu, 0);
        return 1;
    }
    case 0x5e: case 0x5f: set_err(cpu, 0); return 1; /* NMInstall/NMRemove: no notification UI */
    case 0x3b: /* Delay: advance the tick count; nothing sleeps */
        rt->ticks += a0;
        mac_cpu_set(cpu, MAC_D0, rt->ticks);
        return 1;
    case 0x39: { /* ReadDateTime */
        uint32_t secs = (uint32_t)time(NULL) + MAC_EPOCH_DELTA;
        if (mac_cpu_write(cpu, a0, 4, secs)) return -1;
        mac_cpu_write(cpu, 0x20c, 4, secs);
        set_err(cpu, 0);
        return 1;
    }
    case 0x3d: case 0x3e: set_err(cpu, 0); return 1; /* DrvrInstall/DrvrRemove: accepted, no driver table */
    case 0x31: /* GetOSEvent: queue is empty, so a null event */
        if (mac_cpu_write(cpu, a0, 2, 0)) return -1;
        set_err(cpu, 0);
        return 1;
    case 0x77: mac_cpu_set(cpu, MAC_D0, 0); return 1; /* CountADBs: no ADB devices */
    case 0x98: set_err(cpu, 0); return 1; /* HWPriv: no cache or hardware to manage */
    case 0x60: /* FSDispatch / HFSDispatch selectors the file service does not implement */
        if (a0 && !(a0 & 1) && a0 + 18 <= mac_cpu_ram_size(cpu)) mac_cpu_write(cpu, a0 + 16, 2, (uint16_t)-50);
        set_err(cpu, -50);
        return 1;
    default: return 0;
    }
}

/* FSpDispatch (0xAA52): the FSSpec file calls, selected by D0. Argument bytes
 * (excluding the result slot) were read from how real extensions push them:
 *   1 make spec 14, 2/3 open data/resource fork 10, 4 create 14, 5 make directory 10,
 *   6/9/10 delete/lock/unlock 4, 7/8 get/set file info 8, 11 rename 8, 12 move 8,
 *   13 open resource file 6 (returns a refnum), 14 create resource file 14 (no result).
 * The guest has no preferences or support files, so every call reports
 * "file not found" (-43); a spec being made is zeroed. Other selectors stay unhandled. */
static int fsp_dispatch(MacRuntime *rt) {
    static const uint8_t args[15] = {0, 14, 10, 10, 14, 10, 4, 8, 8, 4, 4, 8, 8, 6, 14};
    MacCpu *cpu = rt->cpu;
    uint32_t sel = mac_cpu_get(cpu, MAC_D0), sp = mac_cpu_get(cpu, MAC_A7);
    if (sel < 1 || sel > 14) return 0;
    if (sel == 1) { /* the FSSpec is the last argument: vRefNum, parID, name[64] */
        uint32_t spec = mac_cpu_read(cpu, sp, 4);
        for (uint32_t i = 0; i < 70 && spec; i++) mac_cpu_write(cpu, spec + i, 1, 0);
    }
    pop(cpu, args[sel]);
    if (sel == 14) { mac_cpu_write(cpu, RES_ERR, 2, (uint16_t)-43); return 1; }
    put_result16(cpu, sel == 13 ? 0xffff : (uint16_t)-43);
    if (sel == 13) mac_cpu_write(cpu, RES_ERR, 2, (uint16_t)-43);
    return 1;
}

static int toolbox_trap(MacRuntime *rt, uint16_t opcode) {
    MacCpu *cpu = rt->cpu;
    uint32_t sp = mac_cpu_get(cpu, MAC_A7);
    switch (opcode) {
    case 0xa9c8: pop(cpu, 2); return 1;  /* SysBeep(duration): silent at startup */
    case 0xa99b: case 0xa999: pop(cpu, 2); mac_cpu_write(cpu, RES_ERR, 2, 0); return 1; /* SetResLoad, UpdateResFile */
    case 0xa9aa: case 0xa9b0: pop(cpu, 4); mac_cpu_write(cpu, RES_ERR, 2, 0); return 1;  /* ChangedResource, WriteResource */
    case 0xa9ab: pop(cpu, 14); mac_cpu_write(cpu, RES_ERR, 2, 0); return 1; /* AddResource: accepted, not persisted */
    case 0xa9ba: { /* GetString(id) -> StringHandle */
        int16_t id = (int16_t)mac_cpu_read(cpu, sp, 2);
        pop(cpu, 2);
        mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 4, mac_runtime_resource(rt, 0x53545220u, id));
        return 1;
    }
    case 0xa9a1: { /* GetNamedResource(type, name) -> Handle */
        uint32_t name = mac_cpu_read(cpu, sp, 4), type = mac_cpu_read(cpu, sp + 4, 4);
        int16_t id;
        pop(cpu, 8);
        uint32_t h = find_named(rt, type, name, &id) == 0 ? mac_runtime_resource(rt, type, id) : 0;
        mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 4, h);
        mac_cpu_write(cpu, RES_ERR, 2, h ? 0 : (uint16_t)-192);
        return 1;
    }
    case 0xa9c9: mac_cpu_halt(cpu, "The extension raised a system error (SysError)."); return -1;
    case 0xa9c6: { /* SecondsToDate(secs D0, DateTimeRec* A0) */
        time_t t = (time_t)((int64_t)mac_cpu_get(cpu, MAC_D0) - MAC_EPOCH_DELTA);
        struct tm tm;
        uint32_t a0 = mac_cpu_get(cpu, MAC_A0);
        if (!gmtime_r(&t, &tm)) return 1;
        const int fields[7] = {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_wday + 1};
        for (int i = 0; i < 7; i++) if (mac_cpu_write(cpu, a0 + (uint32_t)i * 2, 2, (uint16_t)fields[i])) return -1;
        return 1;
    }
    case 0xa9c7: { /* DateToSeconds(DateTimeRec* A0) -> secs in D0: the record's year to second, read as the same clock SecondsToDate writes (no time zone) */
        uint32_t a0 = mac_cpu_get(cpu, MAC_A0);
        struct tm tm = {0};
        int f[6];
        for (int i = 0; i < 6; i++) { uint32_t v = mac_cpu_read(cpu, a0 + (uint32_t)i * 2, 2); f[i] = (int16_t)v; }
        tm.tm_year = f[0] - 1900; tm.tm_mon = f[1] - 1; tm.tm_mday = f[2]; tm.tm_hour = f[3]; tm.tm_min = f[4]; tm.tm_sec = f[5];
        int64_t secs = (int64_t)timegm(&tm) + MAC_EPOCH_DELTA;
        mac_cpu_set(cpu, MAC_D0, (uint32_t)secs);
        return 1;
    }
    case 0xa9e1: { /* HandToHand: duplicate the handle in A0 */
        MacHandle *h = by_handle(rt, mac_cpu_get(cpu, MAC_A0));
        if (!h) { mac_cpu_set(cpu, MAC_A0, 0); set_err(cpu, -109); return 1; }
        uint32_t src = h->pointer, size = h->size;
        mac_cpu_set(cpu, MAC_D0, size);
        if (mac_memory_service(rt, 0xa122) != 1 || (int32_t)mac_cpu_get(cpu, MAC_D0) < 0) return 1;
        MacHandle *n = by_handle(rt, mac_cpu_get(cpu, MAC_A0));
        if (n) memcpy(mac_cpu_ram(cpu) + n->pointer, mac_cpu_ram(cpu) + src, size);
        set_err(cpu, 0);
        return 1;
    }
    case 0xabff: { /* DebugStr: no debugger, so the message is only logged */
        uint32_t p = mac_cpu_read(cpu, sp, 4);
        pop(cpu, 4);
        if (rt->trace) fprintf(stderr, "DebugStr at %08x\n", p);
        return 1;
    }
    case 0xa80d: { /* Count1Resources(type) -> short */
        uint32_t type = mac_cpu_read(cpu, sp, 4);
        uint16_t n = 0;
        pop(cpu, 4);
        for (size_t i = 0; i < mac_resource_count(&rt->fork); i++) {
            MacResource r;
            if (!mac_resource_at(&rt->fork, i, &r) && r.type == type) n++;
        }
        put_result16(cpu, n);
        return 1;
    }
    case 0xa80e: { /* Get1IndResource(type, index) -> Handle */
        uint32_t type = mac_cpu_read(cpu, sp + 2, 4), want = mac_cpu_read(cpu, sp, 2), seen = 0, h = 0;
        pop(cpu, 6);
        for (size_t i = 0; i < mac_resource_count(&rt->fork) && !h; i++) {
            MacResource r;
            if (!mac_resource_at(&rt->fork, i, &r) && r.type == type && ++seen == want)
                h = mac_runtime_resource(rt, type, r.id);
        }
        put_result32(cpu, h);
        mac_cpu_write(cpu, RES_ERR, 2, h ? 0 : (uint16_t)-192);
        return 1;
    }
    case 0xa820: { /* Get1NamedResource(type, name) -> Handle */
        uint32_t name = mac_cpu_read(cpu, sp, 4), type = mac_cpu_read(cpu, sp + 4, 4);
        int16_t id;
        pop(cpu, 8);
        uint32_t h = find_named(rt, type, name, &id) == 0 ? mac_runtime_resource(rt, type, id) : 0;
        put_result32(cpu, h);
        mac_cpu_write(cpu, RES_ERR, 2, h ? 0 : (uint16_t)-192);
        return 1;
    }
    case 0xa81a: pop(cpu, 12); put_result16(cpu, 0xffff); mac_cpu_write(cpu, RES_ERR, 2, (uint16_t)-43); return 1; /* HOpenResFile: no resource files besides the INIT's own */
    case 0xa81b: pop(cpu, 10); mac_cpu_write(cpu, RES_ERR, 2, (uint16_t)-43); return 1; /* HCreateResFile: ditto */
    case 0xaa1b: case 0xaa1e: pop(cpu, 2); put_result32(cpu, 0); mac_cpu_write(cpu, RES_ERR, 2, (uint16_t)-192); return 1; /* GetCCursor / GetCIcon: not found */
    case 0xaa1f: pop(cpu, 8); return 1;  /* PlotCIcon: no display at startup */
    case 0xaa25: pop(cpu, 4); return 1;  /* DisposeCIcon */
    case 0xa862: case 0xa892: pop(cpu, 4); return 1; /* ForeColor, Line: nothing is drawn at startup */
    case 0xa8dc: pop(cpu, 8); return 1;  /* CopyRgn: regions are not modified here */
    case 0xa8e5: pop(cpu, 12); return 1; /* UnionRgn: ditto */
    case 0xa85d: { /* BitTst(bytePtr, bitNum) -> Boolean */
        uint32_t base = mac_cpu_read(cpu, sp + 4, 4), bit = mac_cpu_read(cpu, sp, 4);
        pop(cpu, 8);
        uint32_t byte = mac_cpu_read(cpu, base + (bit >> 3), 1);
        mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 2, (byte & (0x80u >> (bit & 7))) ? 0x0100 : 0);
        return 1;
    }
    case 0xa86b: { /* LoWord(v) -> UInt16 */
        uint32_t v = mac_cpu_read(cpu, sp, 4);
        pop(cpu, 4);
        put_result16(cpu, (uint16_t)v);
        return 1;
    }
    case 0xa805: pop(cpu, 10); put_result16(cpu, 0); return 1; /* SndPlay: no audio device at startup; reports success */
    case 0xa97c: pop(cpu, 12); put_result32(cpu, 0); return 1; /* GetNewDialog: no Dialog Manager at startup, returns nil */
    case 0xa895: { /* ShutDown dispatch: 3 = install(proc, flags), 4 = remove(proc) */
        uint32_t sel = mac_cpu_read(cpu, sp, 2);
        if (sel != 3 && sel != 4) return 0;
        pop(cpu, sel == 3 ? 8 : 6);
        return 1;
    }
    case 0xa94b: pop(cpu, 8); return 1;  /* PlotIcon: no display at startup */
    case 0xa874: { /* GetPort(GrafPtr*) */
        uint32_t out = mac_cpu_read(cpu, sp, 4);
        pop(cpu, 4);
        if (mac_cpu_write(cpu, out, 4, rt->current_port)) return -1;
        return 1;
    }
    case 0xa912: return 1; /* InitWindows: the host owns window state */
    case 0xa9a7: pop(cpu, 6); return 1;  /* SetResAttrs: attributes are not tracked per handle */
    case 0xa8a6: { /* EqualRect(r1, r2) -> Boolean */
        uint32_t a = mac_cpu_read(cpu, sp + 4, 4), b = mac_cpu_read(cpu, sp, 4);
        int same = 1;
        pop(cpu, 8);
        for (uint32_t i = 0; i < 8; i += 2) same &= mac_cpu_read(cpu, a + i, 2) == mac_cpu_read(cpu, b + i, 2);
        mac_cpu_write(cpu, mac_cpu_get(cpu, MAC_A7), 2, same ? 0x0100 : 0);
        return 1;
    }
    case 0xa817: pop(cpu, 24); return 1; /* CopyMask: nothing is drawn at startup */
    case 0xabc9: { /* Icon Utilities dispatch: D0 high byte = argument words; PlotIconID-style calls return OSErr */
        uint32_t d0 = mac_cpu_get(cpu, MAC_D0), words = (d0 >> 8) & 0xff;
        if ((d0 & 0xff) != 0 || words < 1 || words > 12 || (d0 >> 16) != 0) return 0;
        pop(cpu, words * 2);
        put_result16(cpu, 0); /* drawing is skipped at startup; reported as success */
        return 1;
    }
    case 0xaa52: return fsp_dispatch(rt);
    default: return 0;
    }
}

int mac_env_service(MacRuntime *rt, uint16_t opcode) {
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return 0;
    return opcode < 0xa800 ? os_trap(rt, opcode) : toolbox_trap(rt, opcode);
}

int mac_env_implemented(unsigned key) {
    switch (key) {
    case 0x1a: case 0x1b: case 0x28: case 0x36: case 0x20: case 0x5e: case 0x5f: case 0x3b:
    case 0x39: case 0x3d: case 0x3e: case 0x31: case 0x77: case 0x98: return 1;
    case 0x60: case 0x9c9: case 0x9c6: case 0x9e1: case 0xbff: case 0x80d: case 0x80e: case 0x820: case 0x81a: case 0x81b:
    case 0x823: case 0xa1b: case 0xa1e: case 0xa1f: case 0xa25: case 0x862: case 0x892: case 0x8dc: case 0x8e5: case 0x85d:
    case 0x86b: case 0x805: case 0x97c: case 0x895: case 0x94b: case 0x874: case 0x912:
    case 0xa52: case 0x9a7: case 0x8a6: case 0x817: case 0xbc9: return 1;
    case 0x9c8: case 0x99b: case 0x999: case 0x9aa: case 0x9b0: case 0x9ab: case 0x9ba: case 0x9a1: return 1;
    default: return 0;
    }
}
