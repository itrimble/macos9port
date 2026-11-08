#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

int mac_palette_manager_service(MacRuntime *, uint16_t);

/* GetNewPalette ($AA92): a Palette object from a 'pltt' resource. Real resources are 16 + 16*count bytes with leftover
 * non-zero bytes in the header and in each entry's private slots, so those fixtures are built the same way. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
#define PLTT 0x706c7474u

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }

/* A fork holding one 'pltt' with the given bytes under the given ID. */
static size_t build_fork(uint8_t *out, int16_t id, const uint8_t *data, size_t size) {
    size_t at = 256, map = at + 4 + size, tl = map + 28, refs = tl + 10, len = (refs - map) + 12 + 2;
    memset(out, 0, map + len);
    put32(out + at, (uint32_t)size); memcpy(out + at + 4, data, size);
    put32(out, 256); put32(out + 4, (uint32_t)map); put32(out + 8, (uint32_t)(4 + size)); put32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); put16(out + map + 24, 28); put16(out + map + 26, (unsigned)(len - 2));
    put16(out + tl, 0); put32(out + tl + 2, PLTT); put16(out + tl + 6, 0); put16(out + tl + 8, 10);
    put16(out + refs, (uint16_t)id); put16(out + refs + 2, 0xffff); put32(out + refs + 4, 0);
    return map + len;
}
/* A resource image: count, garbage header, then entries {rgb, usage, tolerance, garbage private words}. */
static size_t pltt(uint8_t *d, unsigned count) {
    memset(d, 0, 16 + 16 * 300); put16(d, count);
    for (unsigned i = 2; i < 16; i++) d[i] = (uint8_t)(0xa0 + i);                           /* leftover private header bytes */
    for (unsigned i = 0; i < count; i++) {
        uint8_t *e = d + 16 + 16 * i;
        put16(e, (i * 4097) & 0xffff); put16(e + 2, (65535 - i * 257) & 0xffff); put16(e + 4, (i * 31) & 0xffff);
        put16(e + 6, i % 2 ? 2 : 1); put16(e + 8, i * 3);
        put16(e + 10, 0x8002); put16(e + 12, 0xdead); put16(e + 14, 0xbeef);              /* private slots hold junk */
    }
    return 16 + 16 * (size_t)count;
}

static uint8_t fork_bytes[16384];
static MacRuntime *make(int16_t id, const uint8_t *data, size_t size) {
    static MacRuntime r;
    memset(&r, 0, sizeof r);
    size_t n = build_fork(fork_bytes, id, data, size);
    r.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(r.cpu != NULL);
    r.heap_next = 0x100000; r.heap_limit = 0x200000;
    CHECK(!mac_resource_open(&r.fork, fork_bytes, n, NULL, 0));
    return &r;
}
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }
static uint32_t get(MacRuntime *r, int16_t id) {
    wr(r, SP, 2, (uint16_t)id); wr(r, SP + 2, 4, 0xdeadbeef); mac_cpu_set(r->cpu, MAC_A7, SP);
    CHECK(mac_palette_manager_service(r, 0xaa92) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 2);
    return rd(r, SP + 2, 4);
}
static int16_t res_error(MacRuntime *r) { return (int16_t)rd(r, 0xa60, 2); }

static void builds_a_faithful_palette(void) {
    uint8_t d[16 + 16 * 300]; size_t n = pltt(d, 16);
    MacRuntime *r = make(128, d, n);
    size_t before = r->handle_count; uint32_t heap = r->heap_next;
    wr(r, 0x220, 2, 0xffff);
    uint32_t h = get(r, 128);
    CHECK(h != 0 && res_error(r) == 0 && (int16_t)rd(r, 0x220, 2) == 0);
    uint32_t p = rd(r, h, 4);
    CHECK(p && rd(r, p, 2) == 16);
    for (unsigned i = 2; i < 16; i++) CHECK(mac_cpu_ram(r->cpu)[p + i] == 0);              /* private header bytes are not copied */
    for (unsigned i = 0; i < 16; i++) {
        uint32_t e = p + 16 + 16 * i;
        CHECK(rd(r, e, 2) == ((i * 4097) & 0xffff) && rd(r, e + 2, 2) == ((65535 - i * 257) & 0xffff) && rd(r, e + 4, 2) == ((i * 31) & 0xffff));
        CHECK(rd(r, e + 6, 2) == (i % 2 ? 2u : 1u) && rd(r, e + 8, 2) == i * 3);          /* usage and tolerance per entry */
        for (unsigned k = 10; k < 16; k++) CHECK(mac_cpu_ram(r->cpu)[e + k] == 0);          /* private slots are not copied */
    }
    CHECK(r->handle_count == before + 1);                                                    /* only the palette: no resource handle is left loaded */
    CHECK(r->heap_next - heap == 16 + 16 * 16 + 4);
    uint32_t h2 = get(r, 128);
    CHECK(h2 && h2 != h && rd(r, h2, 4) != p);                                               /* every call makes a new palette */
    /* It is a real, owned Palette Manager object: DisposePalette accepts it and frees its record. */
    wr(r, SP, 4, h); mac_cpu_set(r->cpu, MAC_A7, SP);
    CHECK(mac_palette_manager_service(r, 0xaa93) == 1 && !mac_cpu_halted(r->cpu));
    wr(r, SP, 4, h); mac_cpu_set(r->cpu, MAC_A7, SP);
    CHECK(mac_palette_manager_service(r, 0xaa93) == -1 && mac_cpu_halted(r->cpu));         /* disposed: no longer valid */
    mac_cpu_destroy(r->cpu);
}

static void sizes_and_errors(void) {
    uint8_t d[16 + 16 * 300]; size_t n;
    n = pltt(d, 0); MacRuntime *r = make(-16000, d, n);                                      /* empty palette, negative ID */
    uint32_t h = get(r, -16000); CHECK(h && rd(r, rd(r, h, 4), 2) == 0 && res_error(r) == 0);
    CHECK(get(r, 999) == 0 && res_error(r) == -192);                                         /* no such resource */
    mac_cpu_destroy(r->cpu);
    n = pltt(d, 256); r = make(1, d, n);
    h = get(r, 1); CHECK(h && rd(r, rd(r, h, 4), 2) == 256 && res_error(r) == 0);            /* the supported maximum */
    mac_cpu_destroy(r->cpu);
    struct { size_t size; unsigned count; } bad[] = {{15, 0}, {17, 0}, {16 + 16 * 2 - 1, 2}, {16 + 16 * 2 + 1, 2}, {40, 2}, {16, 1}, {16 + 16 * 3, 0xffff}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        memset(d, 0, sizeof d); put16(d, bad[i].count);
        r = make(1, d, bad[i].size); uint32_t heap = r->heap_next, handles = r->handle_count;
        CHECK(get(r, 1) == 0 && res_error(r) == -199 && !mac_cpu_halted(r->cpu));            /* malformed: nil, not a crash */
        CHECK(r->heap_next == heap && r->handle_count == handles);                           /* and nothing allocated */
        mac_cpu_destroy(r->cpu);
    }
    n = pltt(d, 257); r = make(1, d, n);                                                      /* well-formed but beyond the bound */
    wr(r, SP, 2, 1); wr(r, SP + 2, 4, 0); mac_cpu_set(r->cpu, MAC_A7, SP);
    CHECK(mac_palette_manager_service(r, 0xaa92) == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), "256"));
    mac_cpu_destroy(r->cpu);
    n = pltt(d, 8); r = make(1, d, n); r->heap_limit = r->heap_next + 8;                      /* out of memory */
    uint32_t heap = r->heap_next, handles = r->handle_count;
    CHECK(get(r, 1) == 0 && (int16_t)rd(r, 0x220, 2) == -108 && res_error(r) == 0);
    CHECK(r->heap_next == heap && r->handle_count == handles);
    mac_cpu_destroy(r->cpu);
    n = pltt(d, 4); r = make(1, d, n);                                                        /* stack problems halt */
    mac_cpu_set(r->cpu, MAC_A7, SP + 1);
    CHECK(mac_palette_manager_service(r, 0xaa92) == -1 && mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
    r = make(1, d, n); mac_cpu_set(r->cpu, MAC_A7, (uint32_t)mac_cpu_ram_size(r->cpu) - 4);
    CHECK(mac_palette_manager_service(r, 0xaa92) == -1 && mac_cpu_halted(r->cpu));
    mac_cpu_destroy(r->cpu);
}

static void honours_resource_edits(void) {
    uint8_t d[16 + 16 * 300], e[16 + 16 * 300]; size_t n = pltt(d, 4), m = pltt(e, 2);
    MacRuntime *r = make(1, d, n);
    CHECK(mac_resedit_set_data(r, 0, PLTT, 1, e, (uint32_t)m) == 0);                          /* WriteResource-style edit layer */
    uint32_t h = get(r, 1);
    CHECK(h && rd(r, rd(r, h, 4), 2) == 2);                                                   /* the edited palette, not the fork's */
    mac_runtime_destroy(r);
}

int main(void) {
    builds_a_faithful_palette();
    sizes_and_errors();
    honours_resource_edits();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("GetNewPalette checks ok");
    return 0;
}
