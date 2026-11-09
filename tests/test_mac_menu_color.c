/* / NoteAlert / CautionAlert: the native modal alert, driven through the real trap path. */
#include "mac_dialog_draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { BACK = 77 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- a minimal resource-fork writer: header, data, map (types in order of first appearance) ---- */
typedef struct { uint32_t type; int16_t id; uint8_t data[512]; size_t n; } Res;
typedef struct { Res r[48]; unsigned count; } Fork;
static void put16(uint8_t *p, unsigned x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, x >> 16); put16(p + 2, x & 0xffff); }
static void add(Fork *f, uint32_t type, int16_t id, const void *data, size_t n) {
    CHECK(f->count < 48 && n <= sizeof f->r[0].data);
    Res *r = &f->r[f->count++]; r->type = type; r->id = id; r->n = n; memcpy(r->data, data, n);
}
static size_t build(const Fork *f, uint8_t *out) {
    size_t at = 256; memset(out, 0, 16384);
    uint32_t offsets[48];
    for (unsigned i = 0; i < f->count; i++) { offsets[i] = (uint32_t)(at - 256); put32(out + at, (uint32_t)f->r[i].n); memcpy(out + at + 4, f->r[i].data, f->r[i].n); at += 4 + f->r[i].n; }
    size_t map = at; uint32_t types[48]; unsigned ntypes = 0;
    for (unsigned i = 0; i < f->count; i++) { unsigned k = 0; while (k < ntypes && types[k] != f->r[i].type) k++; if (k == ntypes) types[ntypes++] = f->r[i].type; }
    uint8_t *m = out + map; unsigned typelist = 28, refs = typelist + 2 + 8 * ntypes, ref_at = refs;
    put16(m + 24, typelist); put16(m + 26, 0); put16(m + typelist, ntypes - 1);
    for (unsigned t = 0; t < ntypes; t++) {
        unsigned count = 0; for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) count++;
        put32(m + typelist + 2 + t * 8, types[t]); put16(m + typelist + 6 + t * 8, count - 1); put16(m + typelist + 8 + t * 8, ref_at - typelist);
        for (unsigned i = 0; i < f->count; i++) if (f->r[i].type == types[t]) {
            put16(m + ref_at, (unsigned)(uint16_t)f->r[i].id); put16(m + ref_at + 2, 0xffff); m[ref_at + 4] = 0;
            m[ref_at + 5] = (uint8_t)(offsets[i] >> 16); m[ref_at + 6] = (uint8_t)(offsets[i] >> 8); m[ref_at + 7] = (uint8_t)offsets[i];
            ref_at += 12;
        }
    }
    size_t mlen = ref_at; put16(m + 26, (unsigned)mlen); /* the (empty) name list starts at the end of the map */
    put32(out, 256); put32(out + 4, (uint32_t)map); put32(out + 8, (uint32_t)(map - 256)); put32(out + 12, (uint32_t)mlen);
    memcpy(m, out, 16); /* the map starts with a copy of the header */
    return map + mlen;
}


static void menu_resource(Fork *f, int16_t id) {
    uint8_t m[40] = {0}; m[1] = (uint8_t)id; memset(m + 10, 255, 4); m[14] = 4; memcpy(m + 15, "Game", 4); m[19] = 4; memcpy(m + 20, "Quit", 4); m[25] = 'Q';
    add(f, 0x4d454e55, id, m, 29);
}
/* GetMenu on a menu that has a colour table ('mctb') is read like any other: the table is not consulted. */
int main(void) {
    for (unsigned with = 0; with < 2; with++) {
        Fork f = {0}; uint8_t zero[32] = {0}, code[10] = {0, 0, 0, 1, 0xa9, 0xf4};
        put32(zero, 0x100); put32(zero + 4, 0x100); put32(zero + 8, 8); put32(zero + 12, 32);
        put16(zero + 16, 0); put16(zero + 18, 0x3f3c); put16(zero + 20, 1); put16(zero + 22, 0xa9f0);
        add(&f, 0x434f4445, 0, zero, 32); add(&f, 0x434f4445, 1, code, 10);
        menu_resource(&f, 129); if (with) add(&f, 0x6d637462, 129, (const uint8_t[8]){0, 0, 0, 0, 0, 0, 0, 0}, 8);
        uint8_t image[16384]; size_t n = build(&f, image); char err[240]; MacResourceFork fork; MacRuntime rt;
        memset(&rt, 0, sizeof rt);
        if (mac_resource_open(&fork, image, n, err, sizeof err) || mac_runtime_init(&rt, &fork, err, sizeof err)) { fprintf(stderr, "init: %s\n", err); return 1; }
        rt.screen_depth = 8; rt.screen_width = 640; rt.screen_height = 480; rt.screen_row_bytes = 640; rt.screen_base = 0xe00000;
        CHECK(mac_toolbox_service(&rt, 0xa930) == 1);
        mac_cpu_set(rt.cpu, MAC_A7, 0x30000); mac_cpu_write(rt.cpu, 0x30000, 2, 129);
        CHECK(mac_menu_service(&rt, 0xa9bf) == 1 && !mac_cpu_halted(rt.cpu));
        uint32_t h = mac_cpu_read(rt.cpu, 0x30002, 4); CHECK(h != 0);
        mac_cpu_set(rt.cpu, MAC_A7, 0x30000); mac_cpu_write(rt.cpu, 0x30000, 2, 888);                                  /* a missing MENU still fails the same way */
        CHECK(mac_menu_service(&rt, 0xa9bf) == 1 && mac_cpu_read(rt.cpu, 0x30002, 4) == 0);
        mac_runtime_destroy(&rt);
    }
    if (!fails) puts("GetMenu with a colour table checks passed");
    return fails ? 1 : 0;
}
