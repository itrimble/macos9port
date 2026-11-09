/* HandToHand / PtrToHand: copies, error results, and validation of the handle or pointer the guest names. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static MacRuntime fresh(void) {
    MacRuntime rt = {0}; rt.cpu = mac_cpu_create(MAC_RUNTIME_RAM); CHECK(rt.cpu != NULL);
    rt.heap_next = 0x100000; rt.heap_limit = 0x200000; return rt;
}
static uint32_t rd(MacRuntime *r, uint32_t a, unsigned n) { const uint8_t *m = mac_cpu_ram(r->cpu); uint32_t v = 0; for (unsigned i = 0; i < n; i++) v = v << 8 | m[a + i]; return v; }
static uint32_t new_handle(MacRuntime *r, uint32_t size, const char *fill) {
    mac_cpu_set(r->cpu, MAC_D0, size); CHECK(mac_memory_service(r, 0xa122) == 1);
    uint32_t h = mac_cpu_get(r->cpu, MAC_A0); CHECK(h != 0);
    if (fill) memcpy(mac_cpu_ram(r->cpu) + rd(r, h, 4), fill, size);
    return h;
}
static int h2h(MacRuntime *r, uint32_t h) { mac_cpu_set(r->cpu, MAC_A0, h); mac_cpu_set(r->cpu, MAC_D0, 0x12345678); return mac_handcopy_service(r, 0xa9e1); }
static int p2h(MacRuntime *r, uint32_t p, uint32_t n) { mac_cpu_set(r->cpu, MAC_A0, p); mac_cpu_set(r->cpu, MAC_D0, n); return mac_handcopy_service(r, 0xa9e3); }
static int32_t err(MacRuntime *r) { return (int32_t)mac_cpu_get(r->cpu, MAC_D0); }
static int pah(MacRuntime *r, uint32_t p, uint32_t h, uint32_t n) { mac_cpu_set(r->cpu, MAC_A0, p); mac_cpu_set(r->cpu, MAC_A1, h); mac_cpu_set(r->cpu, MAC_D0, n); return mac_handcopy_service(r, 0xa9ef); }
static uint32_t hsize(MacRuntime *r, uint32_t h) { mac_cpu_set(r->cpu, MAC_A0, h); mac_cpu_set(r->cpu, MAC_D0, 0); CHECK(mac_memory_service(r, 0xa025) == 1); return mac_cpu_get(r->cpu, MAC_D0); }
/* PtrAndHand: A0 = pointer, A1 = handle, D0 = count; the bytes are appended to the handle's block. */
static void ptr_and_hand(void) {
    MacRuntime r = fresh(); uint32_t h = new_handle(&r, 5, "hello");
    memcpy(mac_cpu_ram(r.cpu) + 0x3000, "WORLD!!", 7);
    CHECK(pah(&r, 0x3000, h, 6) == 1 && err(&r) == 0 && rd(&r, 0x220, 2) == 0 && (mac_cpu_get(r.cpu, MAC_SR) & 15) == 4);
    CHECK(mac_cpu_get(r.cpu, MAC_A0) == 0x3000 && mac_cpu_get(r.cpu, MAC_A1) == h);
    CHECK(hsize(&r, h) == 11 && !memcmp(mac_cpu_ram(r.cpu) + rd(&r, h, 4), "helloWORLD!", 11));
    CHECK(pah(&r, 0x3000, h, 0) == 1 && err(&r) == 0 && hsize(&r, h) == 11);                            /* zero bytes: nothing changes, the source is not even looked at */
    CHECK(pah(&r, 0, h, 0) == 1 && err(&r) == 0);
    {   uint32_t e = new_handle(&r, 0, NULL); CHECK(pah(&r, 0x3000, e, 3) == 1 && err(&r) == 0 && hsize(&r, e) == 3 && !memcmp(mac_cpu_ram(r.cpu) + rd(&r, e, 4), "WOR", 3)); }   /* onto an empty handle */
    {   uint32_t sp = rd(&r, h, 4); CHECK(pah(&r, sp, h, 3) == 1 && err(&r) == 0 && hsize(&r, h) == 14 && !memcmp(mac_cpu_ram(r.cpu) + rd(&r, h, 4) + 11, "hel", 3)); }   /* the source is the handle's own block */
    /* errors append nothing */
    CHECK(pah(&r, 0x3000, 0, 3) == 1 && err(&r) == -109);                                                 /* nil handle */
    CHECK(pah(&r, 0x3000, h + 1, 3) == 1 && err(&r) == -109);                                             /* odd */
    CHECK(pah(&r, 0x3000, 0x3000, 3) == 1 && err(&r) == -109);                                            /* not in the handle table */
    CHECK(pah(&r, 0xfffff0, h, 64) == 1 && err(&r) == -111 && hsize(&r, h) == 14);                         /* source beyond RAM */
    CHECK(pah(&r, 0x3000, h, 0x2000000) == 1 && err(&r) == -111 && hsize(&r, h) == 14);                    /* absurd count */
    CHECK(pah(&r, 0x3000, h, 0xffffffffu) == 1 && err(&r) == -111);
    {   uint32_t master = rd(&r, h, 4); mac_cpu_write(r.cpu, h, 4, master + 16); CHECK(pah(&r, 0x3000, h, 3) == 1 && err(&r) == -111); mac_cpu_write(r.cpu, h, 4, 0); CHECK(pah(&r, 0x3000, h, 3) == 1 && err(&r) == -111); mac_cpu_write(r.cpu, h, 4, master); }   /* moved or purged master pointer */
    CHECK(hsize(&r, h) == 14);
    for (unsigned k = 0; k < 2; k++) {                                                                        /* resource and loaded-code handles are not extended */
        for (size_t i = 0; i < r.handle_count; i++) if (r.handles[i].handle == h) { if (k) r.handles[i].loaded_code = 1; else r.handles[i].resource = 1; }
        CHECK(pah(&r, 0x3000, h, 3) == 1 && err(&r) == -111);
        for (size_t i = 0; i < r.handle_count; i++) if (r.handles[i].handle == h) { r.handles[i].loaded_code = 0; r.handles[i].resource = 0; }
    }
    CHECK(pah(&r, 0x3000, h, 1) == 1 && err(&r) == 0 && hsize(&r, h) == 15);
    {   uint32_t l = new_handle(&r, 4, "lock"); mac_cpu_set(r.cpu, MAC_A0, l); CHECK(mac_memory_service(&r, 0xa029) == 1);                 /* HLock: SetHandleSize refuses, nothing is appended */
        CHECK(pah(&r, 0x3000, l, 4) == 1 && err(&r) != 0 && hsize(&r, l) == 4 && !memcmp(mac_cpu_ram(r.cpu) + rd(&r, l, 4), "lock", 4)); }
    mac_cpu_destroy(r.cpu);
}
int main(void) {
    ptr_and_hand();
    MacRuntime r = fresh();
    uint32_t h = new_handle(&r, 5, "hello");
    CHECK(mac_handcopy_service(&r, 0xa9e2) == 0 && mac_handcopy_service(&r, 0xa9e0) == 0 && mac_handcopy_service(&r, 0xa122) == 0);   /* neighbours are not ours */
    CHECK(h2h(&r, h) == 1 && err(&r) == 0 && (mac_cpu_get(r.cpu, MAC_SR) & 15) == 4);
    uint32_t c = mac_cpu_get(r.cpu, MAC_A0), cp = rd(&r, c, 4), hp = rd(&r, h, 4);
    CHECK(c != 0 && c != h && cp != hp && !memcmp(mac_cpu_ram(r.cpu) + cp, "hello", 5) && rd(&r, 0x220, 2) == 0);   /* a separate block with the same bytes */
    mac_cpu_ram(r.cpu)[cp] = 'J'; CHECK(mac_cpu_ram(r.cpu)[hp] == 'h');                                                    /* ... independent of the original */
    mac_cpu_set(r.cpu, MAC_D0, 0); mac_cpu_set(r.cpu, MAC_A0, c); CHECK(mac_memory_service(&r, 0xa025) == 1 && mac_cpu_get(r.cpu, MAC_D0) == 5);   /* GetHandleSize sees 5 */
    {   uint32_t z = new_handle(&r, 0, NULL);                                                                              /* an empty handle copies to an empty handle */
        CHECK(h2h(&r, z) == 1 && err(&r) == 0 && mac_cpu_get(r.cpu, MAC_A0) != 0); }
    memcpy(mac_cpu_ram(r.cpu) + 0x3000, "ABCDEFG", 7);
    CHECK(p2h(&r, 0x3000, 7) == 1 && err(&r) == 0 && !memcmp(mac_cpu_ram(r.cpu) + rd(&r, mac_cpu_get(r.cpu, MAC_A0), 4), "ABCDEFG", 7));   /* PtrToHand */
    CHECK(p2h(&r, 0x3000, 0) == 1 && err(&r) == 0 && mac_cpu_get(r.cpu, MAC_A0) != 0);                                    /* zero bytes: an empty handle, source unused */
    CHECK(p2h(&r, 0, 0) == 1 && err(&r) == 0);
    /* errors leave A0 nil and set the result */
    CHECK(h2h(&r, 0) == 1 && err(&r) == -109 && mac_cpu_get(r.cpu, MAC_A0) == 0 && rd(&r, 0x220, 2) == 0xff93 && (mac_cpu_get(r.cpu, MAC_SR) & 15) == 8);   /* nil handle */
    CHECK(h2h(&r, 0x3001) == 1 && err(&r) == -109);                                                                          /* odd, not a handle */
    CHECK(h2h(&r, 0x3000) == 1 && err(&r) == -109);                                                                          /* a pointer, not in the handle table */
    CHECK(h2h(&r, 0xffffff00u) == 1 && err(&r) == -109);
    mac_cpu_write(r.cpu, h, 4, 0); CHECK(h2h(&r, h) == 1 && err(&r) == -112 && mac_cpu_get(r.cpu, MAC_A0) == 0);          /* purged (master pointer nil) */
    mac_cpu_write(r.cpu, h, 4, hp + 2); CHECK(h2h(&r, h) == 1 && err(&r) == -111);                                        /* master pointer changed behind our back */
    mac_cpu_write(r.cpu, h, 4, hp);
    CHECK(p2h(&r, 0x3000, 0x2000000u) == 1 && err(&r) == -111 && mac_cpu_get(r.cpu, MAC_A0) == 0);                        /* absurd size */
    CHECK(p2h(&r, MAC_RUNTIME_RAM - 4, 8) == 1 && err(&r) == -111);                                                        /* source past the end of RAM */
    CHECK(p2h(&r, 0x100, 4) == 1 && err(&r) == -111);                                                                      /* source in low memory */
    {   uint32_t used = r.heap_next, count = r.handle_count; r.heap_limit = r.heap_next + 6;                               /* no room: memFullErr, nothing kept */
        CHECK(h2h(&r, h) == 1 && err(&r) == -108 && mac_cpu_get(r.cpu, MAC_A0) == 0 && r.heap_next == used && r.handle_count == count); }
    r.heap_limit = 0x200000;
    {   MacRuntime q = fresh(); uint32_t qh = new_handle(&q, 3, "abc");                                                    /* a full handle table: memFullErr */
        q.handle_count = MAC_RUNTIME_HANDLES; for (size_t i = 0; i < MAC_RUNTIME_HANDLES; i++) if (!q.handles[i].handle && !q.handles[i].pointer) q.handles[i].pointer = 1;
        uint32_t used = q.heap_next; CHECK(h2h(&q, qh) == 1 && err(&q) == -108 && q.heap_next == used);
        mac_cpu_destroy(q.cpu); }
    {   MacRuntime q = fresh(); mac_cpu_halt(q.cpu, "x"); CHECK(h2h(&q, 0) == -1); mac_cpu_destroy(q.cpu); }
    mac_cpu_destroy(r.cpu);
    if (!fails) puts("HandToHand checks passed");
    return fails ? 1 : 0;
}
