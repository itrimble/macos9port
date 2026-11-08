#include "mac_init.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HOST_DATA = 256, HOST_MAP = 384, HOST_TOTAL = HOST_MAP + 62 };

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }

/* Authored resource fork: CODE 0 (one near jump-table entry) and CODE 1 (RTS).
 * Static because the runtime keeps referring to it. */
static const MacResourceFork *host_fork(void) {
    static uint8_t bytes[HOST_TOTAL];
    static MacResourceFork fork;
    static int ready;
    if (ready) return &fork;
    uint8_t *b = bytes;
    put32(b, HOST_DATA); put32(b + 4, HOST_MAP); put32(b + 8, 38); put32(b + 12, 62);
    put32(b + HOST_DATA, 24);
    put32(b + HOST_DATA + 4, 0x100); put32(b + HOST_DATA + 8, 0x100);
    put32(b + HOST_DATA + 12, 8); put32(b + HOST_DATA + 16, 32);
    put16(b + HOST_DATA + 20, 0); put16(b + HOST_DATA + 22, 0x3f3c);
    put16(b + HOST_DATA + 24, 1); put16(b + HOST_DATA + 26, 0xa9f0);
    put32(b + HOST_DATA + 28, 6);
    put16(b + HOST_DATA + 32, 0); put16(b + HOST_DATA + 34, 1); put16(b + HOST_DATA + 36, 0x4e75);
    put16(b + HOST_MAP + 24, 28); put16(b + HOST_MAP + 26, 62);
    put16(b + HOST_MAP + 28, 0); put32(b + HOST_MAP + 30, MAC_CODE_TYPE);
    put16(b + HOST_MAP + 34, 1); put16(b + HOST_MAP + 36, 10);
    for (unsigned i = 0; i < 2; i++) {
        put16(b + HOST_MAP + 38 + i * 12, (uint16_t)i);
        put16(b + HOST_MAP + 40 + i * 12, 0xffff);
    }
    b[HOST_MAP + 50 + 7] = 28;
    char err[96];
    if (mac_resource_open(&fork, bytes, HOST_TOTAL, err, sizeof err) != 0) return NULL;
    ready = 1;
    return &fork;
}

int mac_init_host_create(MacRuntime *rt, char *err, size_t n) {
    const MacResourceFork *fork = host_fork();
    if (!fork) {
        if (err && n) snprintf(err, n, "Could not build the extension host fork.");
        return -1;
    }
    return mac_runtime_init(rt, fork, err, n);
}

typedef struct {
    uint32_t traps[4096];
    uint32_t sel[MAC_GESTALT_MAX], val[MAC_GESTALT_MAX], proc[MAC_GESTALT_MAX];
    unsigned gestalt_count;
    uint32_t heap_next;
    uint32_t regs[18];
} Snapshot;

static void save(MacRuntime *rt, Snapshot *s) {
    memcpy(s->traps, rt->trap_addresses, sizeof s->traps);
    memcpy(s->sel, rt->gestalt_sel, sizeof s->sel);
    memcpy(s->val, rt->gestalt_val, sizeof s->val);
    memcpy(s->proc, rt->gestalt_proc, sizeof s->proc);
    s->gestalt_count = rt->gestalt_count;
    s->heap_next = rt->heap_next;
    for (int i = 0; i < 18; i++) s->regs[i] = mac_cpu_get(rt->cpu, (MacCpuReg)i);
}

static void restore_regs(MacRuntime *rt, const Snapshot *s) {
    for (int i = 0; i < 18; i++) mac_cpu_set(rt->cpu, (MacCpuReg)i, s->regs[i]);
}

static void rollback(MacRuntime *rt, const Snapshot *s) {
    memcpy(rt->trap_addresses, s->traps, sizeof s->traps);
    memcpy(rt->gestalt_sel, s->sel, sizeof s->sel);
    memcpy(rt->gestalt_val, s->val, sizeof s->val);
    memcpy(rt->gestalt_proc, s->proc, sizeof s->proc);
    rt->gestalt_count = s->gestalt_count;
    rt->heap_next = s->heap_next;
}

static int cmp_id(const void *a, const void *b) {
    return (int)*(const int16_t *)a - (int)*(const int16_t *)b;
}

static int fail(MacInitReport *rep, const char *message) {
    if (rep) snprintf(rep->error, sizeof rep->error, "%s", message);
    return -1;
}

int mac_init_run(MacRuntime *rt, const MacResourceFork *fork, size_t budget, MacInitReport *rep) {
    if (rep) memset(rep, 0, sizeof *rep);
    if (!rt || !rt->cpu || !fork || mac_cpu_halted(rt->cpu)) return fail(rep, "No usable runtime.");
    int16_t ids[256];
    size_t total = mac_resource_count(fork), n = 0;
    for (size_t i = 0; i < total && n < 256; i++) {
        MacResource r;
        if (mac_resource_at(fork, i, &r) == 0 && r.type == MAC_INIT_TYPE) ids[n++] = r.id;
    }
    qsort(ids, n, sizeof ids[0], cmp_id);
    static Snapshot snap; /* single execution thread, see mac_cpu.h */
    save(rt, &snap);
    rt->init_guard = 1;
    rt->init_violation = 0;
    int result = 0;
    for (size_t k = 0; k < n && result == 0; k++) {
        MacResource r;
        if (mac_resource_find(fork, MAC_INIT_TYPE, ids[k], &r) != 0 || r.size < 2 || r.size > 0x100000) {
            result = fail(rep, "INIT resource is empty or too large.");
            break;
        }
        uint32_t code = mac_runtime_alloc(rt, (uint32_t)r.size + 8);
        if (!code) { result = fail(rep, "Out of guest memory for INIT."); break; }
        uint32_t sentinel = code + (((uint32_t)r.size + 3) & ~3u);
        memcpy(mac_cpu_ram(rt->cpu) + code, r.data, r.size);
        mac_cpu_write(rt->cpu, sentinel, 2, 0x4e71);
        uint32_t sp = rt->stack_base - 64;
        mac_cpu_write(rt->cpu, sp - 4, 4, sentinel);
        mac_cpu_set(rt->cpu, MAC_A7, sp - 4);
        mac_cpu_set(rt->cpu, MAC_A5, rt->a5);
        mac_cpu_set(rt->cpu, MAC_D0, 0);
        mac_cpu_set(rt->cpu, MAC_SR, 0);
        mac_cpu_set(rt->cpu, MAC_PC, code);
        size_t used = 0;
        while (used < budget && !mac_cpu_halted(rt->cpu) && mac_cpu_get(rt->cpu, MAC_PC) != sentinel) {
            size_t ran = mac_cpu_run(rt->cpu, 1);
            if (!ran) break;
            used += ran;
        }
        if (mac_cpu_halted(rt->cpu)) {
            if (rep) snprintf(rep->error, sizeof rep->error, "%s",
                              mac_cpu_error(rt->cpu) ? mac_cpu_error(rt->cpu) : "halted");
            rt->init_guard = 0;
            return -2;
        }
        if (rt->init_violation) result = fail(rep, "INIT tried to patch a protected trap.");
        else if (mac_cpu_get(rt->cpu, MAC_PC) != sentinel) result = fail(rep, "INIT did not return within its instruction budget.");
        else if (rep) rep->count++;
    }
    rt->init_guard = 0;
    if (result != 0) rollback(rt, &snap);
    restore_regs(rt, &snap);
    return result;
}
