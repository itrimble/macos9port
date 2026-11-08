/* Scrap Manager subset: ZeroScrap, PutScrap, GetScrap (absent type) and guest-argument validation. */
#include "mac_scrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define RAM 0x40000u
#define SP 0xe000u
#define TEXT 0x54455854u
#define PICT 0x50494354u
static MacRuntime fresh(void) { MacRuntime rt = {0}; rt.cpu = mac_cpu_create(RAM); CHECK(rt.cpu != NULL); return rt; }
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { const uint8_t *m = mac_cpu_ram(r->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | m[a + i]; return v; }
static void w(MacRuntime *r, uint32_t a, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, a, n, v); }
static int zero(MacRuntime *r) { mac_cpu_set(r->cpu, MAC_A7, SP); w(r, SP, 4, 0x55555555); return mac_scrap_service(r, 0xa9fc); }
static int put(MacRuntime *r, int32_t len, uint32_t type, uint32_t src) { mac_cpu_set(r->cpu, MAC_A7, SP); w(r, SP, 4, src); w(r, SP + 4, 4, type); w(r, SP + 8, 4, (uint32_t)len); w(r, SP + 12, 4, 0x55555555); return mac_scrap_service(r, 0xa9fe); }
static int get(MacRuntime *r, uint32_t type, uint32_t dest, uint32_t off) { mac_cpu_set(r->cpu, MAC_A7, SP); w(r, SP, 4, off); w(r, SP + 4, 4, type); w(r, SP + 8, 4, dest); w(r, SP + 12, 4, 0x55555555); return mac_scrap_service(r, 0xa9fd); }
int main(void) {
    MacRuntime r = fresh();
    memset(mac_cpu_ram(r.cpu) + 0x2000, 0xee, 64);
    CHECK(mac_scrap_service(&r, 0xa9ff) == 0 && mac_scrap_service(&r, 0xa000) == 0 && mac_scrap_service(&r, 0xa9f8) == 0);   /* neighbours are not ours */
    CHECK(zero(&r) == 1 && rd(&r, SP, 4) == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP);                                     /* ZeroScrap: noErr, no arguments to pop */
    CHECK(get(&r, TEXT, 0x3000, 0x2000) == 1 && (int32_t)rd(&r, SP + 12, 4) == -102 && mac_cpu_get(r.cpu, MAC_A7) == SP + 12 && rd(&r, 0x2000, 1) == 0xee);   /* empty: noTypeErr, offset untouched */
    memcpy(mac_cpu_ram(r.cpu) + 0x2100, "hello", 5);
    CHECK(put(&r, 5, TEXT, 0x2100) == 1 && rd(&r, SP + 12, 4) == 0 && mac_cpu_get(r.cpu, MAC_A7) == SP + 12);          /* PutScrap: noErr, 12 bytes of arguments popped */
    CHECK(get(&r, PICT, 0x3000, 0x2000) == 1 && (int32_t)rd(&r, SP + 12, 4) == -102);                                  /* another type is still absent */
    CHECK(put(&r, 0, PICT, 0) == 1 && put(&r, 5, TEXT, 0x2100) == 1);                                                  /* zero length needs no source; same type replaces */
    CHECK(zero(&r) == 1 && get(&r, TEXT, 0x3000, 0x2000) == 1 && (int32_t)rd(&r, SP + 12, 4) == -102);                /* ZeroScrap forgets everything */
    CHECK(put(&r, 5, TEXT, 0x2100) == 1 && get(&r, TEXT, 0x3000, 0x2000) == -1 && mac_cpu_halted(r.cpu));              /* a present type: halts by name (not implemented) */
    mac_scrap_destroy(&r); mac_cpu_destroy(r.cpu);
    /* UnloadScrap ($A9FA) and LoadScrap ($A9FB): no arguments, a LONGINT result of noErr, the scrap itself untouched; a bad stack halts. */
    for (unsigned k = 0; k < 2; k++) {
        MacRuntime q = fresh(); unsigned op = k ? 0xa9fb : 0xa9fa;
        memcpy(mac_cpu_ram(q.cpu) + 0x2100, "hello", 5); CHECK(put(&q, 5, TEXT, 0x2100) == 1);
        mac_cpu_set(q.cpu, MAC_A7, SP); w(&q, SP, 4, 0x55555555);
        CHECK(mac_scrap_service(&q, (uint16_t)op) == 1 && rd(&q, SP, 4) == 0 && mac_cpu_get(q.cpu, MAC_A7) == SP && !mac_cpu_halted(q.cpu));
        CHECK(get(&q, PICT, 0x3000, 0x2000) == 1 && (int32_t)rd(&q, SP + 12, 4) == -102);                  /* still usable, still holding TEXT only */
        CHECK(get(&q, TEXT, 0x3000, 0x2000) == -1 && mac_cpu_halted(q.cpu));                                /* TEXT survived Load/Unload (a present type halts by name) */
        mac_scrap_destroy(&q); mac_cpu_destroy(q.cpu);
        MacRuntime b = fresh(); mac_cpu_set(b.cpu, MAC_A7, SP + 1); CHECK(mac_scrap_service(&b, (uint16_t)op) == -1 && mac_cpu_halted(b.cpu)); mac_cpu_destroy(b.cpu);
        MacRuntime c = fresh(); mac_cpu_set(c.cpu, MAC_A7, (uint32_t)mac_cpu_ram_size(c.cpu) - 2); CHECK(mac_scrap_service(&c, (uint16_t)op) == -1); mac_cpu_destroy(c.cpu);
    }
    {   /* corrupt arguments halt and change nothing */
        struct { int kind; } k[] = { {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7} };
        for (unsigned i = 0; i < sizeof k / sizeof *k; i++) {
            MacRuntime q = fresh(); memset(mac_cpu_ram(q.cpu) + 0x2000, 0xee, 64); int rc;
            switch (i) {
            case 0: rc = put(&q, -1, TEXT, 0x2100); break;                       /* negative length */
            case 1: rc = put(&q, 65537, TEXT, 0x2100); break;                    /* over the cap */
            case 2: rc = put(&q, 8, TEXT, RAM - 4); break;                       /* source runs off the end */
            case 3: rc = put(&q, 4, TEXT, 0x100); break;                         /* source in low memory */
            case 4: rc = get(&q, TEXT, 0x3000, 0); break;                        /* null offset pointer */
            case 5: rc = get(&q, TEXT, 0x3000, RAM - 2); break;                  /* offset pointer runs off the end */
            case 6: mac_cpu_set(q.cpu, MAC_A7, SP + 1); rc = mac_scrap_service(&q, 0xa9fe); break;           /* odd stack */
            default: mac_cpu_set(q.cpu, MAC_A7, RAM - 8); rc = mac_scrap_service(&q, 0xa9fe); break;          /* no room for the arguments */
            }
            CHECK(rc == -1 && mac_cpu_halted(q.cpu) && rd(&q, 0x2000, 1) == 0xee); mac_scrap_destroy(&q); mac_cpu_destroy(q.cpu);
        }
        MacRuntime q = fresh();                                                  /* the type table is bounded: a ninth type halts, a repeat does not */
        for (uint32_t t = 0; t < 8; t++) CHECK(put(&q, 0, 0x41000000u + t, 0) == 1);
        CHECK(put(&q, 0, 0x41000003u, 0) == 1 && put(&q, 0, 0x42000000u, 0) == -1 && mac_cpu_halted(q.cpu));
        mac_scrap_destroy(&q); mac_cpu_destroy(q.cpu);
        q = fresh(); CHECK(put(&q, 65536, TEXT, 0x100) == -1); mac_scrap_destroy(&q); mac_cpu_destroy(q.cpu);                /* exactly the cap passes the length test (then the source test halts) */
    }
    {   /* SetResLoad (mac_resload.c): accepted, its Boolean word popped; bad stacks halt; other traps are not its */
        MacRuntime q = fresh(); mac_cpu_set(q.cpu, MAC_A7, SP); w(&q, SP, 2, 0x0000);
        CHECK(mac_resload_service(&q, 0xa99b) == 1 && mac_cpu_get(q.cpu, MAC_A7) == SP + 2 && !mac_cpu_halted(q.cpu));
        mac_cpu_set(q.cpu, MAC_A7, SP); CHECK(mac_resload_service(&q, 0xa99a) == 0 && mac_resload_service(&q, 0xa99c) == 0 && mac_cpu_get(q.cpu, MAC_A7) == SP);
        mac_cpu_set(q.cpu, MAC_A7, RAM - 2); CHECK(mac_resload_service(&q, 0xa99b) == 1 && mac_cpu_get(q.cpu, MAC_A7) == RAM);   /* the last two bytes of RAM are enough */
        mac_cpu_set(q.cpu, MAC_A7, RAM - 1); CHECK(mac_resload_service(&q, 0xa99b) == -1 && mac_cpu_halted(q.cpu)); mac_cpu_destroy(q.cpu);
        q = fresh(); mac_cpu_set(q.cpu, MAC_A7, 0x100); CHECK(mac_resload_service(&q, 0xa99b) == -1 && mac_cpu_halted(q.cpu)); mac_cpu_destroy(q.cpu);
        q = fresh(); mac_cpu_set(q.cpu, MAC_A7, SP + 1); CHECK(mac_resload_service(&q, 0xa99b) == -1 && mac_cpu_halted(q.cpu)); mac_cpu_destroy(q.cpu);
        q = fresh(); mac_cpu_set(q.cpu, MAC_A7, RAM + 2); CHECK(mac_resload_service(&q, 0xa99b) == -1); mac_cpu_destroy(q.cpu);
    }
    if (!fails) puts("Scrap Manager checks passed");
    return fails ? 1 : 0;
}
