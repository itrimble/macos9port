#include "mac_runtime.h"

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
    default: return 0;
    }
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
    case 0x9c8: case 0x99b: case 0x999: case 0x9aa: case 0x9b0: case 0x9ab: case 0x9ba: case 0x9a1: return 1;
    default: return 0;
    }
}
