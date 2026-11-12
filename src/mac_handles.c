#include "mac_handles.h"
#include <string.h>

typedef struct { uint32_t d0, a0, sr, memerr; } Saved;
static Saved save(MacRuntime *rt) {
    MacCpu *c = rt->cpu;
    return (Saved){mac_cpu_get(c, MAC_D0), mac_cpu_get(c, MAC_A0), mac_cpu_get(c, MAC_SR), mac_cpu_read(c, 0x220, 2)};
}
static void restore(MacRuntime *rt, Saved s) {
    MacCpu *c = rt->cpu;
    mac_cpu_set(c, MAC_D0, s.d0); mac_cpu_set(c, MAC_A0, s.a0); mac_cpu_set(c, MAC_SR, s.sr); mac_cpu_write(c, 0x220, 2, s.memerr);
}
static uint32_t make(MacRuntime *rt, uint32_t size, uint16_t trap) {
    if (!rt || !rt->cpu || mac_cpu_halted(rt->cpu)) return 0;
    Saved s = save(rt);
    mac_cpu_set(rt->cpu, MAC_D0, size);
    uint32_t out = 0;
    if (mac_memory_service(rt, trap) == 1 && (int16_t)mac_cpu_read(rt->cpu, 0x220, 2) == 0 && !mac_cpu_halted(rt->cpu)) out = mac_cpu_get(rt->cpu, MAC_A0);
    restore(rt, s);
    return out;
}
uint32_t mac_handle_new(MacRuntime *rt, uint32_t size) {
    uint32_t h = make(rt, size, 0xa322), p, n; /* NewHandleClear */
    if (h && mac_handle_block(rt, h, &p, &n)) memset(mac_cpu_ram(rt->cpu) + p, 0, n < size ? n : size);
    return h;
}
uint32_t mac_ptr_new(MacRuntime *rt, uint32_t size) {
    uint32_t p = make(rt, size, 0xa31e); /* NewPtrClear */
    size_t z = p ? mac_cpu_ram_size(rt->cpu) : 0;
    if (p && p <= z && size <= z - p) memset(mac_cpu_ram(rt->cpu) + p, 0, size);
    return p;
}
void mac_handle_dispose(MacRuntime *rt, uint32_t handle) {
    if (!rt || !rt->cpu || !handle || mac_cpu_halted(rt->cpu)) return;
    Saved s = save(rt);
    mac_cpu_set(rt->cpu, MAC_A0, handle);
    mac_memory_service(rt, 0xa023);
    restore(rt, s);
}
int mac_handle_block(const MacRuntime *rt, uint32_t handle, uint32_t *pointer, uint32_t *size) {
    size_t z = mac_cpu_ram_size(rt->cpu);
    if (!handle || (handle & 1) || handle > z || z - handle < 4) return 0;
    for (size_t i = 0; i < rt->handle_count; i++) {
        const MacHandle *m = &rt->handles[i];
        if (m->handle != handle) continue;
        uint32_t p = mac_cpu_read(rt->cpu, handle, 4);
        if (!p || p != m->pointer || p > z || m->size > z - p) return 0;
        *pointer = p; *size = m->size;
        return 1;
    }
    return 0;
}
