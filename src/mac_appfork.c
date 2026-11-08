#include "mac_runtime.h"
#include "mac_resource_chain.h"
#include <string.h>

/* The application's own resource file, as the classic runtime exposes it to code that reads it directly.
 *
 * Some applications (THINK C's startup among them) do not go through the Resource Manager to find their initial data:
 * they take the file reference from low-memory CurApRefNum ($900), walk the resource map that TopMapHndl ($A50) points
 * to in guest memory, and read the bytes of a resource with PBRead at the offset the map gives. Without these, that
 * startup finds no map, decides there is nothing to initialise, and leaves its globals zero.
 *
 * Published here: CurApRefNum and CurMap (the application's resource file reference, MAC_APP_RES_REF), and TopMapHndl, a
 * handle to a copy of the file's resource map (the in-memory map layout is the file layout with the next-map handle and
 * the file reference number filled in). The copy lives in an unused part of guest RAM above the screen so that no
 * application heap address moves. PBRead (A002) and PBGetEOF (A011) on that reference read the fork bytes, read-only; every
 * other call on it is not claimed and fails as an unknown reference. Resource handles inside the map copy stay zero:
 * the Resource Manager here keeps its own handles. */
enum { MAP_HANDLE = 0xe60000, MAP_BYTES = MAP_HANDLE + 16, MAP_LIMIT = 0x18000 };

void mac_appfork_publish(MacRuntime *rt) {
    MacCpu *cpu = rt->cpu;
    const MacResourceFork *f = &rt->fork;
    mac_cpu_write(cpu, 0x900, 2, MAC_APP_RES_REF); /* CurApRefNum */
    mac_cpu_write(cpu, 0xa5a, 2, MAC_APP_RES_REF); /* CurMap */
    if (!f->bytes || f->map_size < 28 || f->map_size > MAP_LIMIT || f->map_offset > f->size || f->map_size > f->size - f->map_offset) return;
    uint8_t *map = mac_cpu_ram(cpu) + MAP_BYTES;
    memcpy(map, f->bytes + f->map_offset, f->map_size);
    memset(map + 16, 0, 4);                        /* handle to the next map: none */
    map[20] = 0; map[21] = (uint8_t)MAC_APP_RES_REF; /* file reference number */
    mac_cpu_write(cpu, MAP_HANDLE, 4, MAP_BYTES);  /* the handle's master pointer */
    mac_cpu_write(cpu, 0xa50, 4, MAP_HANDLE);      /* TopMapHndl */
}

static int done(MacCpu *cpu, uint32_t pb, int error) {
    mac_cpu_write(cpu, pb + 16, 2, (uint16_t)error);
    mac_cpu_set(cpu, MAC_D0, (uint32_t)(int32_t)error);
    uint32_t sr = mac_cpu_get(cpu, MAC_SR) & ~15u;
    mac_cpu_set(cpu, MAC_SR, sr | (error < 0 ? 8u : error == 0 ? 4u : 0u));
    return 1;
}

int mac_appfork_service(MacRuntime *rt, uint16_t opcode) {
    unsigned base = opcode & ~0x400u;
    if (base != 0xa002 && base != 0xa011) return 0;
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return -1;
    MacCpu *cpu = rt->cpu;
    uint32_t pb = mac_cpu_get(cpu, MAC_A0), z = (uint32_t)mac_cpu_ram_size(cpu);
    if ((pb & 1) || pb < 0x400 || pb > z || z - pb < 56) return 0; /* not a usable block: let the File Manager complain */
    if ((int16_t)mac_cpu_read(cpu, pb + 24, 2) != MAC_APP_RES_REF) return 0;
    if (!rt->fork.bytes) return 0;
    if (opcode & 0x400) return done(cpu, pb, -50); /* asynchronous calls are not supported */
    uint32_t size = (uint32_t)rt->fork.size;
    if (base == 0xa011) { mac_cpu_write(cpu, pb + 28, 4, size); return done(cpu, pb, 0); } /* PBGetEOF: ioMisc */
    uint32_t buffer = mac_cpu_read(cpu, pb + 32, 4), request = mac_cpu_read(cpu, pb + 36, 4);
    unsigned mode = mac_cpu_read(cpu, pb + 44, 2) & 3;
    int32_t offset = (int32_t)mac_cpu_read(cpu, pb + 46, 4);
    int64_t at = mode == 0 ? (int64_t)rt->appfork_mark : mode == 1 ? offset : mode == 2 ? (int64_t)size + offset : (int64_t)rt->appfork_mark + offset;
    mac_cpu_write(cpu, pb + 40, 4, 0);
    if (at < 0) return done(cpu, pb, -40);         /* posErr */
    uint32_t n = at >= size ? 0 : (uint32_t)((int64_t)size - at < (int64_t)request ? (int64_t)size - at : request);
    if (n && (buffer < 0x400 || buffer > z || z - buffer < n)) { mac_cpu_halt(cpu, "PBRead on the application fork has an invalid buffer."); return -1; }
    if (n) memcpy(mac_cpu_ram(cpu) + buffer, rt->fork.bytes + at, n);
    rt->appfork_mark = (uint32_t)(at + n);
    mac_cpu_write(cpu, pb + 40, 4, n);             /* ioActCount */
    mac_cpu_write(cpu, pb + 46, 4, rt->appfork_mark); /* ioPosOffset: the new mark */
    return done(cpu, pb, n < request ? -39 : 0);   /* eofErr when short */
}
