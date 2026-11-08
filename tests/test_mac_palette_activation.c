#include "mac_runtime.h"
#include "mac_window_int.h"

#include <stdio.h>
#include <string.h>

int mac_window_resource_service(MacRuntime *, uint16_t);
int mac_palette_manager_service(MacRuntime *, uint16_t);

/* Palette activation on the 8-bit main device: SetPalette on a visible frontmost window, ShowWindow of a window that has
 * a palette, and ActivatePalette. Expectations are stated as invariants (the requested colors really are on the device,
 * indices 0 and 255 survive, nothing else changes) plus a few policy cases. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
#define WIND 0x57494e44u
#define PLTT 0x706c7474u

typedef struct { uint32_t type; int16_t id; const uint8_t *data; size_t size; } Res;
typedef struct { unsigned r, g, b, usage, tol; } Color;
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }

static size_t build_fork(uint8_t *out, const Res *list, unsigned n) {
    size_t at = 256, data_len = 0; uint32_t off[8];
    for (unsigned i = 0; i < n; i++) { off[i] = (uint32_t)data_len; put32(out + at + data_len, (uint32_t)list[i].size);
        memcpy(out + at + data_len + 4, list[i].data, list[i].size); data_len += 4 + list[i].size; }
    size_t map = at + data_len; uint32_t types[8]; unsigned nt = 0;
    for (unsigned i = 0; i < n; i++) { unsigned k = 0; while (k < nt && types[k] != list[i].type) k++; if (k == nt) types[nt++] = list[i].type; }
    size_t tl = map + 28, refs = tl + 2 + 8 * nt, len = (refs - map) + 12 * n + 2;
    memset(out + map, 0, len);
    put32(out, 256); put32(out + 4, (uint32_t)map); put32(out + 8, (uint32_t)data_len); put32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); put16(out + map + 24, 28); put16(out + map + 26, (unsigned)(len - 2)); put16(out + tl, nt - 1);
    unsigned ref = 0;
    for (unsigned t = 0; t < nt; t++) {
        unsigned count = 0; for (unsigned i = 0; i < n; i++) count += list[i].type == types[t];
        put32(out + tl + 2 + 8 * t, types[t]); put16(out + tl + 6 + 8 * t, count - 1); put16(out + tl + 8 + 8 * t, (unsigned)(refs - tl + 12 * ref));
        for (unsigned i = 0; i < n; i++) if (list[i].type == types[t]) { uint8_t *r = out + refs + 12 * ref++; put16(r, (uint16_t)list[i].id); put16(r + 2, 0xffff); put32(r + 4, off[i]); }
    }
    return map + len;
}
static size_t wind(uint8_t *d, int visible) {
    memset(d, 0, 32); put16(d, 80); put16(d + 2, 90); put16(d + 4, 280); put16(d + 6, 390); put16(d + 8, 2);
    d[10] = (uint8_t)visible; d[18] = 4; memcpy(d + 19, "Test", 4); return 23;
}
static size_t pltt(uint8_t *d, const Color *c, unsigned n) {          /* colors given as 8-bit values, stored as 16-bit */
    memset(d, 0, 16 + 16 * n); put16(d, n);
    for (unsigned i = 0; i < n; i++) {
        uint8_t *e = d + 16 + 16 * i;
        put16(e, c[i].r * 257); put16(e + 2, c[i].g * 257); put16(e + 4, c[i].b * 257); put16(e + 6, c[i].usage); put16(e + 8, c[i].tol);
    }
    return 16 + 16 * (size_t)n;
}

static uint8_t fork_bytes[16384];
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(r->cpu, p, n, v); }

/* A runtime with an 8-bit device, one window (WIND 128; visible or hidden) and palettes 200.. from `sets`. */
static uint8_t wind_bytes[64], pl_bytes[4][16 + 16 * 300];
static MacRuntime *make(int visible, const Color *const *sets, const unsigned *counts, unsigned nsets) {
    static MacRuntime r;
    memset(&r, 0, sizeof r);
    Res list[6]; unsigned n = 0;
    list[n++] = (Res){WIND, 128, wind_bytes, wind(wind_bytes, visible)};
    for (unsigned i = 0; i < nsets; i++) list[n++] = (Res){PLTT, (int16_t)(200 + i), pl_bytes[i], pltt(pl_bytes[i], sets[i], counts[i])};
    size_t size = build_fork(fork_bytes, list, n);
    r.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(r.cpu != NULL);
    r.heap_next = 0x100000; r.heap_limit = 0x600000; r.qd_globals = 0x7000; r.modifiers = 0x80;
    CHECK(!mac_resource_open(&r.fork, fork_bytes, size, NULL, 0));
    CHECK(mac_color_init(&r) == 1);
    mac_cpu_set(r.cpu, MAC_A7, SP); CHECK(mac_toolbox_service(&r, 0xa912) == 1);
    uint8_t *ram = mac_cpu_ram(r.cpu);
    memset(ram + SP - 294, 0x5a, 308); put32(ram + SP, UINT32_MAX); put32(ram + SP + 4, 0); put16(ram + SP + 8, 128);
    mac_cpu_set(r.cpu, MAC_A7, SP);
    CHECK(mac_window_resource_service(&r, 0xaa46) == 1 && r.window.address);
    return &r;
}
static uint32_t new_palette(MacRuntime *r, int id) {
    wr(r, SP, 2, (uint16_t)id); wr(r, SP + 2, 4, 0); mac_cpu_set(r->cpu, MAC_A7, SP);
    CHECK(mac_palette_manager_service(r, 0xaa92) == 1);
    return rd(r, SP + 2, 4);
}
static int set_palette(MacRuntime *r, uint32_t window, uint32_t pal) {
    wr(r, SP, 2, 0x0100); wr(r, SP + 2, 4, pal); wr(r, SP + 6, 4, window); mac_cpu_set(r->cpu, MAC_A7, SP);
    return mac_palette_manager_service(r, 0xaa95);
}
static int activate(MacRuntime *r, uint32_t window) { wr(r, SP, 4, window); mac_cpu_set(r->cpu, MAC_A7, SP); return mac_palette_manager_service(r, 0xaa94); }
static int show(MacRuntime *r, uint32_t window) { wr(r, SP, 4, window); mac_cpu_set(r->cpu, MAC_A7, SP); return mac_window_service(r, 0xa915); }
static void snapshot(MacRuntime *r, uint32_t t[256]) { memcpy(t, r->palette, 256 * sizeof t[0]); }
static uint32_t rgb8(unsigned r, unsigned g, unsigned b) { return r << 16 | g << 8 | b; }
static int has(const uint32_t t[256], uint32_t c) { for (unsigned i = 0; i < 256; i++) if (t[i] == c) return 1; return 0; }
static unsigned changed(const uint32_t a[256], const uint32_t b[256]) { unsigned n = 0; for (unsigned i = 0; i < 256; i++) n += a[i] != b[i]; return n; }
/* The guest-visible color table must agree with the runtime's 8-bit view for every entry. */
static int table_consistent(MacRuntime *r) {
    uint32_t ct = rd(r, r->screen_colors, 4);
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = r->palette[i];
        if ((rd(r, ct + 10 + i * 8, 2) >> 8) != ((c >> 16) & 255) || (rd(r, ct + 12 + i * 8, 2) >> 8) != ((c >> 8) & 255) || (rd(r, ct + 14 + i * 8, 2) >> 8) != (c & 255)) return 0;
    }
    return 1;
}
static const uint8_t *ct_raw(MacRuntime *r) { uint8_t *ram = mac_cpu_ram(r->cpu); uint32_t h = r->screen_colors, ct = (uint32_t)(ram[h] << 24 | ram[h + 1] << 16 | ram[h + 2] << 8 | ram[h + 3]); return ram + ct; }
static void clear_update(MacRuntime *r) { uint32_t region = rd(r, r->window.handles[10], 4); wr(r, region, 2, 10); for (unsigned k = 0; k < 4; k++) wr(r, region + 2 + 2 * k, 2, 0); }
static uint32_t update_box_top(MacRuntime *r) { return rd(r, rd(r, r->window.handles[10], 4) + 2, 2); }

/* Glider 4.10's real palette: 16 entries, all pmTolerant with tolerance 0. */
static const Color GLIDER[16] = {
    {255,255,255,2,0},{0,0,0,2,0},{255,100,2,2,0},{221,8,6,2,0},{242,8,132,2,0},{70,0,165,2,0},{0,0,212,2,0},{2,171,234,2,0},
    {31,183,20,2,0},{0,100,17,2,0},{86,44,5,2,0},{144,113,58,2,0},{192,192,192,2,0},{128,128,128,2,0},{64,64,64,2,0},{252,243,5,2,0}};

static void glider_palette_on_visible_window(void) {
    const Color *sets[1] = {GLIDER}; unsigned counts[1] = {16};
    MacRuntime *r = make(1, sets, counts, 1);
    uint32_t base[256], after[256]; snapshot(r, base);
    uint32_t pal = new_palette(r, 200); CHECK(pal != 0);
    uint32_t seed = rd(r, rd(r, r->screen_colors, 4), 4);
    clear_update(r); CHECK(update_box_top(r) == 0);
    CHECK(set_palette(r, r->window.address, pal) == 1 && !mac_cpu_halted(r->cpu) && mac_cpu_get(r->cpu, MAC_A7) == SP + 10);
    snapshot(r, after);
    unsigned missing = 0, distinct[16], nd = 0;
    for (unsigned i = 0; i < 16; i++) {
        uint32_t c = rgb8(GLIDER[i].r, GLIDER[i].g, GLIDER[i].b);
        CHECK(has(after, c) && r->palette[r->palette_map[i]] == c);                          /* the exact color is on the device */
        if (!has(base, c)) { unsigned seen = 0; for (unsigned k = 0; k < nd; k++) seen |= distinct[k] == c; if (!seen) distinct[nd++] = c; missing++; }
    }
    CHECK(changed(base, after) == nd && nd > 0);                                              /* exactly the colors that were missing were allocated */
    CHECK(after[0] == base[0] && after[255] == base[255]);                                    /* white and black survive */
    CHECK(r->palette_map[0] == 0 && r->palette_map[1] == 255);                                /* white and black reuse their entries */
    unsigned used[256] = {0};
    for (unsigned i = 0; i < 16; i++) { CHECK(used[r->palette_map[i]] == 0 || rgb8(GLIDER[i].r, GLIDER[i].g, GLIDER[i].b) == r->palette[r->palette_map[i]]); used[r->palette_map[i]]++; }
    CHECK(table_consistent(r));
    CHECK(rd(r, rd(r, r->screen_colors, 4), 4) != seed && r->palette_active == pal);          /* ctSeed moved; the active palette is recorded */
    CHECK(update_box_top(r) == (uint16_t)r->window.bounds[0]);                                /* the window was invalidated to redraw */
    (void)missing;
    CHECK(activate(r, r->window.address) == 1 && !changed(after, r->palette) && table_consistent(r));   /* ActivatePalette again: idempotent */
    mac_cpu_destroy(r->cpu);
}

static void policy_cases(void) {
    /* A color one step from a known entry replaces that entry (nearest-first), not some other one. */
    uint32_t probe_base[256];
    static const Color near_a[1] = {{0, 0, 0, 2, 0}};   /* placeholder, replaced below */
    (void)near_a;
    MacRuntime *r = make(1, (const Color *const[]){GLIDER}, (unsigned[]){16}, 1);
    snapshot(r, probe_base);
    unsigned target = 77; uint32_t c = probe_base[target];
    Color near[1] = {{((c >> 16) & 255) ^ 1, (c >> 8) & 255, c & 255, 2, 0}};
    mac_cpu_destroy(r->cpu);
    const Color *sets[1] = {near}; unsigned counts[1] = {1};
    r = make(1, sets, counts, 1);
    uint32_t pal = new_palette(r, 200), after[256];
    CHECK(set_palette(r, r->window.address, pal) == 1);
    snapshot(r, after);
    CHECK(r->palette_map[0] == target && changed(probe_base, after) == 1 && after[target] == rgb8(near[0].r, near[0].g, near[0].b));
    mac_cpu_destroy(r->cpu);

    /* White (0) and black (255) are never reallocated, even when a request is nearest to them. */
    Color edge[2] = {{254, 255, 255, 2, 0}, {0, 1, 0, 2, 0}};
    sets[0] = edge; counts[0] = 2;
    r = make(1, sets, counts, 1);
    snapshot(r, probe_base); pal = new_palette(r, 200);
    CHECK(set_palette(r, r->window.address, pal) == 1 && r->palette[0] == 0xffffff && r->palette[255] == 0);
    CHECK(r->palette_map[0] != 0 && r->palette_map[1] != 255 && changed(probe_base, r->palette) == 2);
    CHECK(r->palette[r->palette_map[0]] == rgb8(254, 255, 255) && r->palette[r->palette_map[1]] == rgb8(0, 1, 0));
    mac_cpu_destroy(r->cpu);

    /* Courteous entries record a nearest color and change nothing; the window is not invalidated. */
    Color court[3] = {{255, 100, 2, 0, 0}, {1, 2, 3, 0, 0}, {0, 0, 0, 0x30, 0}};            /* usage 0, and pmWhite/pmBlack hints */
    sets[0] = court; counts[0] = 3;
    r = make(1, sets, counts, 1);
    snapshot(r, probe_base); pal = new_palette(r, 200); clear_update(r);
    CHECK(set_palette(r, r->window.address, pal) == 1 && !changed(probe_base, r->palette) && update_box_top(r) == 0);   /* nothing changed: nothing to redraw */
    for (unsigned i = 0; i < 3; i++) CHECK(r->palette_map[i] < 256);
    mac_cpu_destroy(r->cpu);

    /* Inhibit bits: pmInhibitC8 skips an entry on a color device, pmInhibitG8 on a gray one; the other still applies. */
    Color inh[2] = {{1, 2, 3, 2 | 0x2000, 0}, {5, 6, 7, 2 | 0x1000, 0}};
    sets[0] = inh; counts[0] = 2;
    r = make(1, sets, counts, 1);
    snapshot(r, probe_base); pal = new_palette(r, 200);
    CHECK(set_palette(r, r->window.address, pal) == 1);
    CHECK(!has(r->palette, rgb8(1, 2, 3)) && has(r->palette, rgb8(5, 6, 7)) && changed(probe_base, r->palette) == 1);   /* colour device: C8 entry skipped */
    CHECK(mac_color_set_gray(r, 1) == 0 && activate(r, r->window.address) == 1);
    CHECK(has(r->palette, rgb8(1, 2, 3)) && !has(r->palette, rgb8(5, 6, 7)));                                           /* gray device: G8 entry skipped */
    CHECK(table_consistent(r));
    mac_cpu_destroy(r->cpu);

    /* On a gray device a gray the ramp already holds needs no entry; a colored one does. */
    Color mixed[2] = {{128, 128, 128, 2, 0}, {200, 50, 50, 2, 0}};
    sets[0] = mixed; counts[0] = 2;
    r = make(1, sets, counts, 1);
    CHECK(mac_color_set_gray(r, 1) == 0); snapshot(r, probe_base); pal = new_palette(r, 200);
    CHECK(set_palette(r, r->window.address, pal) == 1 && changed(probe_base, r->palette) == 1 && has(r->palette, rgb8(200, 50, 50)));
    mac_cpu_destroy(r->cpu);
}

static void switching_and_visibility(void) {
    Color a[1] = {{1, 2, 3, 2, 0}}, b[1] = {{9, 8, 7, 2, 0}};
    const Color *sets[2] = {a, b}; unsigned counts[2] = {1, 1};
    MacRuntime *r = make(1, sets, counts, 2);
    uint32_t base[256]; snapshot(r, base);
    uint32_t pa = new_palette(r, 200), pb = new_palette(r, 201);
    CHECK(set_palette(r, r->window.address, pa) == 1 && has(r->palette, rgb8(1, 2, 3)));
    CHECK(set_palette(r, r->window.address, pb) == 1);                                        /* a new palette starts from a fresh table */
    CHECK(!has(r->palette, rgb8(1, 2, 3)) && has(r->palette, rgb8(9, 8, 7)) && changed(base, r->palette) == 1);
    mac_cpu_destroy(r->cpu);

    /* A hidden window only records the association; showing it realizes the palette. */
    sets[0] = a; counts[0] = 1;
    r = make(0, sets, counts, 1);
    snapshot(r, base); pa = new_palette(r, 200);
    CHECK(!r->window.visible && set_palette(r, r->window.address, pa) == 1 && !changed(base, r->palette));
    CHECK(activate(r, r->window.address) == 1 && !changed(base, r->palette));                 /* ActivatePalette on a hidden window: no change */
    CHECK(show(r, r->window.address) == 1 && r->window.visible && has(r->palette, rgb8(1, 2, 3)) && table_consistent(r));
    mac_cpu_destroy(r->cpu);

    /* A window with no palette: ActivatePalette is a harmless no-op. */
    r = make(1, sets, counts, 1);
    snapshot(r, base);
    CHECK(activate(r, r->window.address) == 1 && !changed(base, r->palette) && r->palette_active == 0);
    mac_cpu_destroy(r->cpu);
}

static void refusals_change_nothing(void) {
    const unsigned bad[] = {1, 4, 8, 0x0c, 0x0e, 3, 0x4000, 0x8002, 0x10000 >> 1};            /* dithered, animated, explicit, unknown modifiers */
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        Color c[2] = {{1, 2, 3, 2, 0}, {9, 8, 7, bad[i], 0}};
        const Color *sets[1] = {c}; unsigned counts[1] = {2};
        MacRuntime *r = make(1, sets, counts, 1);
        uint32_t base[256]; snapshot(r, base); uint32_t pal = new_palette(r, 200);
        uint8_t guest[2056]; memcpy(guest, ct_raw(r), sizeof guest);
        CHECK(table_consistent(r));
        int rc = set_palette(r, r->window.address, pal);
        CHECK(rc == -1 && mac_cpu_halted(r->cpu) && !changed(base, r->palette));             /* refused... */
        CHECK(!memcmp(guest, ct_raw(r), sizeof guest));                                         /* ...before any change, in the guest table too */
        mac_cpu_destroy(r->cpu);
    }
    {   /* ShowWindow refuses before the window appears */
        Color c[1] = {{9, 8, 7, 4, 0}};
        const Color *sets[1] = {c}; unsigned counts[1] = {1};
        MacRuntime *r = make(0, sets, counts, 1);
        uint32_t pal = new_palette(r, 200);
        CHECK(set_palette(r, r->window.address, pal) == 1 && !r->window.visible);
        CHECK(show(r, r->window.address) == -1 && mac_cpu_halted(r->cpu) && !r->window.visible);
        mac_cpu_destroy(r->cpu);
    }
    {   /* capacity: 254 distinct non-cube colors fit exactly, 255 do not */
        static Color many[255];
        for (unsigned i = 0; i < 255; i++) many[i] = (Color){1 + i % 35, 2 + i / 35, 3, 2, 0};
        const Color *sets[1] = {many}; unsigned counts[1] = {254};
        MacRuntime *r = make(1, sets, counts, 1); uint32_t pal = new_palette(r, 200);
        CHECK(set_palette(r, r->window.address, pal) == 1 && r->palette[0] == 0xffffff && r->palette[255] == 0 && table_consistent(r));
        for (unsigned i = 0; i < 254; i++) CHECK(r->palette[r->palette_map[i]] == rgb8(many[i].r, many[i].g, many[i].b));
        mac_cpu_destroy(r->cpu);
        counts[0] = 255; r = make(1, sets, counts, 1); pal = new_palette(r, 200);
        uint32_t base[256]; snapshot(r, base);
        CHECK(set_palette(r, r->window.address, pal) == -1 && mac_cpu_halted(r->cpu) && !changed(base, r->palette));
        mac_cpu_destroy(r->cpu);
    }
    {   /* bad arguments and the wrong display */
        const Color *sets[1] = {GLIDER}; unsigned counts[1] = {16};
        MacRuntime *r = make(1, sets, counts, 1);
        CHECK(activate(r, 0x1234) == -1 && mac_cpu_halted(r->cpu)); mac_cpu_destroy(r->cpu);
        r = make(1, sets, counts, 1); mac_cpu_set(r->cpu, MAC_A7, (uint32_t)mac_cpu_ram_size(r->cpu) - 2);
        CHECK(mac_palette_manager_service(r, 0xaa94) == -1 && mac_cpu_halted(r->cpu)); mac_cpu_destroy(r->cpu);
        r = make(1, sets, counts, 1); uint32_t pal = new_palette(r, 200); r->screen_depth = 1;
        CHECK(set_palette(r, r->window.address, pal) == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), "8-bit"));
        mac_cpu_destroy(r->cpu);
    }
}

int main(void) {
    glider_palette_on_visible_window();
    policy_cases();
    switching_and_visibility();
    refusals_change_nothing();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("palette activation checks ok");
    return 0;
}
