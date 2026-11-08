#include "mac_runtime.h"

#include <stdio.h>
#include <string.h>

#include "mac_window_int.h"
int mac_window_resource_service(MacRuntime *, uint16_t);

/* 'wctb' window color tables through GetNewCWindow ($AA46): parsing, drawing, isolation between windows. Pixels are
 * read back from the framebuffer, and the expected palette entry is computed independently here. */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
#define WIND 0x57494e44u
#define WCTB 0x77637462u
#define TOP 80
#define LEFT 90
#define BOTTOM 280
#define RIGHT 390

typedef struct { uint32_t type; int16_t id; const uint8_t *data; size_t size; } Res;
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

/* A WIND: bounds, procID, visible, goAway, refCon, title. */
static size_t wind(uint8_t *d, unsigned proc, int visible, unsigned top, unsigned left, unsigned bottom, unsigned right) {
    memset(d, 0, 32); put16(d, top); put16(d + 2, left); put16(d + 4, bottom); put16(d + 6, right); put16(d + 8, proc);
    d[10] = (uint8_t)visible; d[12] = 0; put32(d + 14, 0); d[18] = 4; memcpy(d + 19, "Test", 4); return 23;
}
/* A compiled wctb from {part, r, g, b} records. */
static size_t wctb(uint8_t *d, unsigned entries, const unsigned (*rec)[4]) {
    memset(d, 0, 8); put16(d + 6, entries - 1);
    for (unsigned i = 0; i < entries; i++) for (unsigned k = 0; k < 4; k++) put16(d + 8 + 8 * i + 2 * k, rec[i][k]);
    return 8 + 8 * entries;
}

static uint8_t fork_bytes[4096];
typedef struct { MacRuntime rt; } Rig;
static MacRuntime *make(const Res *list, unsigned n) {
    static MacRuntime r;
    memset(&r, 0, sizeof r);
    size_t size = build_fork(fork_bytes, list, n);
    r.cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    CHECK(r.cpu != NULL);
    r.heap_next = 0x100000; r.heap_limit = 0x600000; r.qd_globals = 0x7000; r.modifiers = 0x80;
    CHECK(!mac_resource_open(&r.fork, fork_bytes, size, NULL, 0));
    CHECK(mac_color_init(&r) == 1);
    mac_cpu_set(r.cpu, MAC_A7, SP); CHECK(mac_toolbox_service(&r, 0xa912) == 1);
    return &r;
}
static int open_window(MacRuntime *r, int id) {
    uint8_t *ram = mac_cpu_ram(r->cpu);
    memset(ram + SP - 294, 0x5a, 308);
    put32(ram + SP, UINT32_MAX); put32(ram + SP + 4, 0); put16(ram + SP + 8, (unsigned)id);
    mac_cpu_set(r->cpu, MAC_A7, SP);
    return mac_window_resource_service(r, 0xaa46);
}
static uint8_t pixel(MacRuntime *r, int x, int y) { return mac_cpu_ram(r->cpu)[r->screen_base + (uint32_t)y * r->screen_row_bytes + (uint32_t)x]; }
/* Independent nearest-palette search (8-bit RGB distance), mirroring what the spec asks of a color request. */
static uint8_t closest(MacRuntime *r, unsigned r16, unsigned g16, unsigned b16) {
    unsigned best = 0; long bd = -1;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t c = r->palette[i]; long dr = (long)((c >> 16) & 255) - (long)(r16 >> 8), dg = (long)((c >> 8) & 255) - (long)(g16 >> 8), db = (long)(c & 255) - (long)(b16 >> 8);
        long d = dr * dr + dg * dg + db * db; if (bd < 0 || d < bd) { bd = d; best = i; }
    }
    return (uint8_t)best;
}
static unsigned darkest(MacRuntime *r) { unsigned best = 0, min = ~0u; for (unsigned i = 0; i < 256; i++) { uint32_t c = r->palette[i]; unsigned v = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); if (v < min) { min = v; best = i; } } return best; }
static unsigned lightest(MacRuntime *r) { unsigned best = 0, max = 0; for (unsigned i = 0; i < 256; i++) { uint32_t c = r->palette[i]; unsigned v = ((c >> 16) & 255) + ((c >> 8) & 255) + (c & 255); if (v >= max) { max = v; best = i; } } return best; }

/* Glider 4.10's real main-window table: black content, frame and text; reserved parts 3 and 4 (black, white). */
static const unsigned GLIDER[5][4] = {{0,0,0,0},{1,0,0,0},{2,0,0,0},{3,0,0,0},{4,0xffff,0xffff,0xffff}};

static void real_table_on_plain_box(void) {
    uint8_t w[64], c[64]; size_t wn = wind(w, 2, 1, TOP, LEFT, BOTTOM, RIGHT), cn = wctb(c, 5, GLIDER);
    const Res with[] = {{WIND, 129, w, wn}, {WCTB, 129, c, cn}}, without[] = {{WIND, 129, w, wn}};
    MacRuntime *r = make(without, 1);
    CHECK(open_window(r, 129) == 1 && r->window.address && r->window.color_set == 0);
    uint8_t default_content = pixel(r, 200, 150), default_frame = pixel(r, LEFT - 1, 150);
    CHECK(default_content == lightest(r) && default_frame == darkest(r));              /* white content, dark outline */
    mac_cpu_destroy(r->cpu);
    r = make(with, 2);
    CHECK(open_window(r, 129) == 1 && r->window.address);
    CHECK(r->window.color_set == 7);                                                      /* parts 0, 1, 2 recorded; 3 and 4 are not */
    CHECK(pixel(r, 200, 150) == darkest(r) && pixel(r, LEFT, TOP) == darkest(r) && pixel(r, RIGHT - 1, BOTTOM - 1) == darkest(r));   /* black content */
    CHECK(pixel(r, LEFT - 1, 150) == darkest(r) && pixel(r, 200, TOP - 1) == darkest(r));                                          /* black frame */
    CHECK(pixel(r, LEFT - 2, 150) == mac_cpu_ram(r->cpu)[r->screen_base + 150 * r->screen_row_bytes + LEFT - 2]);                  /* untouched outside */
    CHECK(mac_cpu_halted(r->cpu) == 0 && (int16_t)mac_cpu_read(r->cpu, 0xa60, 2) == 0);
    mac_cpu_destroy(r->cpu);
}

static void distinct_colors_and_titled_windows(void) {
    uint8_t w[64], c[64];
    const unsigned custom[3][4] = {{0, 0xffff, 0, 0}, {1, 0, 0, 0xffff}, {2, 0, 0xffff, 0}};   /* red content, blue frame, green text */
    size_t wn = wind(w, 2, 1, TOP, LEFT, BOTTOM, RIGHT), cn = wctb(c, 3, custom);
    Res list[] = {{WIND, 129, w, wn}, {WCTB, 129, c, cn}};
    MacRuntime *r = make(list, 2);
    CHECK(open_window(r, 129) == 1);
    uint8_t red = closest(r, 0xffff, 0, 0), blue = closest(r, 0, 0, 0xffff);
    CHECK(red != blue && pixel(r, 200, 150) == red && pixel(r, LEFT - 1, 150) == blue && pixel(r, 200, BOTTOM) == blue);
    CHECK(pixel(r, 200, 150) != lightest(r));                                           /* content colour really replaced white */
    mac_cpu_destroy(r->cpu);

    wn = wind(w, 12, 1, TOP, LEFT, BOTTOM, RIGHT);                                      /* titled zoomNoGrow window */
    list[0].size = wn;
    r = make(list, 2);
    CHECK(open_window(r, 129) == 1 && r->window.proc_id == 12);
    uint8_t green = closest(r, 0, 0xffff, 0);
    CHECK(pixel(r, 200, 150) == red);                                                    /* content */
    CHECK(pixel(r, LEFT + 50, TOP - 20) == blue && pixel(r, LEFT - 1, 150) == blue && pixel(r, LEFT + 50, TOP - 1) == blue);   /* frame, title-bar edge */
    CHECK(pixel(r, LEFT + 6, TOP - 18) == lightest(r));                                  /* title-bar fill keeps its default colour */
    unsigned text = 0, stray = 0;                                                        /* title characters use the text colour */
    for (int y = TOP - 19; y < TOP - 1; y++) for (int x = LEFT + 20; x < RIGHT - 20; x++) { if (pixel(r, x, y) == green) text++; }
    for (int y = TOP - 19; y < TOP - 1; y++) for (int x = LEFT + 20; x < RIGHT - 20; x++) { uint8_t p = pixel(r, x, y); if (p != green && p != lightest(r)) stray++; }
    CHECK(text > 10 && stray == 0);
    mac_cpu_destroy(r->cpu);
}

static void unused_or_missing_parts_change_nothing(void) {
    uint8_t w[64], c[64];
    const unsigned reserved[2][4] = {{3, 0xffff, 0, 0}, {4, 0, 0xffff, 0}};               /* documented as reserved */
    size_t wn = wind(w, 2, 1, TOP, LEFT, BOTTOM, RIGHT), cn = wctb(c, 2, reserved);
    Res list[] = {{WIND, 129, w, wn}, {WCTB, 129, c, cn}};
    MacRuntime *r = make(list, 2);
    CHECK(open_window(r, 129) == 1 && r->window.color_set == 0);
    CHECK(pixel(r, 200, 150) == lightest(r) && pixel(r, LEFT - 1, 150) == darkest(r));   /* identical to no table */
    mac_cpu_destroy(r->cpu);
    const unsigned other[1][4] = {{0, 0xffff, 0, 0}};                                     /* a table for a different resource ID */
    cn = wctb(c, 1, other); Res stray[] = {{WIND, 129, w, wn}, {WCTB, 130, c, cn}};
    r = make(stray, 2);
    CHECK(open_window(r, 129) == 1 && r->window.color_set == 0 && pixel(r, 200, 150) == lightest(r));
    mac_cpu_destroy(r->cpu);
}

static void rejects_what_it_cannot_draw(void) {
    uint8_t w[64], c[256]; size_t wn = wind(w, 2, 1, TOP, LEFT, BOTTOM, RIGHT);
    struct { const char *name; unsigned entries; unsigned rec[4][4]; const char *message; } cases[] = {
        {"stripes", 2, {{0,0,0,0},{5,0,0,0}}, "unsupported"}, {"dialog frame", 1, {{9,0,0,0}}, "unsupported"},
        {"tinge", 1, {{12,0,0,0}}, "unsupported"}, {"unknown part", 1, {{13,0,0,0}}, "unknown part"},
        {"duplicate", 2, {{0,0,0,0},{0,1,1,1}}, "repeats"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        size_t cn = wctb(c, cases[i].entries, (const unsigned (*)[4])cases[i].rec);
        Res list[] = {{WIND, 129, w, wn}, {WCTB, 129, c, cn}};
        MacRuntime *r = make(list, 2);
        uint8_t before[8]; memcpy(before, mac_cpu_ram(r->cpu) + SP, 8);
        int rc = open_window(r, 129);
        CHECK(rc == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), cases[i].message));
        CHECK(r->window.address == 0);                                                    /* no window was created */
        mac_cpu_destroy(r->cpu);
    }
    struct { const char *name; size_t size; unsigned count; } bad[] = {{"too short", 5, 0}, {"count beyond cap", 8 + 8 * 14, 13}, {"size mismatch short", 8 + 8, 2}, {"negative", 8, 0xffff}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        memset(c, 0, sizeof c); put16(c + 6, bad[i].count);
        Res list[] = {{WIND, 129, w, wn}, {WCTB, 129, c, bad[i].size}};
        MacRuntime *r = make(list, 2);
        CHECK(open_window(r, 129) == -1 && mac_cpu_halted(r->cpu) && r->window.address == 0);
        mac_cpu_destroy(r->cpu);
    }
}

/* A valid one-entry table followed by trailing bytes is not an exact image of the structure: reject, don't guess. */
static void rejects_trailing_bytes(void) {
    uint8_t w[64], c[64]; const unsigned one[1][4] = {{0, 0xffff, 0, 0}};
    size_t wn = wind(w, 2, 1, TOP, LEFT, BOTTOM, RIGHT), cn = wctb(c, 1, one);
    memset(c + cn, 0, 8);
    Res list[] = {{WIND, 129, w, wn}, {WCTB, 129, c, cn + 8}};
    MacRuntime *r = make(list, 2);
    CHECK(open_window(r, 129) == -1 && mac_cpu_halted(r->cpu) && strstr(mac_cpu_error(r->cpu), "size") && r->window.address == 0);
    mac_cpu_destroy(r->cpu);
    list[1].size = cn;                                                                   /* the same table, exact size, is fine */
    r = make(list, 2);
    CHECK(open_window(r, 129) == 1 && r->window.color_set == 1);
    mac_cpu_destroy(r->cpu);
}

static void isolation_visibility_and_palette(void) {
    uint8_t w1[64], w2[64], c[64]; const unsigned red[1][4] = {{0, 0xffff, 0, 0}};
    size_t n1 = wind(w1, 2, 1, TOP, LEFT, BOTTOM, RIGHT), n2 = wind(w2, 2, 0, 300, 100, 400, 300), cn = wctb(c, 1, red);
    Res list[] = {{WIND, 129, w1, n1}, {WIND, 130, w2, n2}, {WCTB, 129, c, cn}};
    MacRuntime *r = make(list, 3);
    CHECK(open_window(r, 129) == 1);
    uint32_t first = r->window.address;
    CHECK(open_window(r, 130) == 1 && r->window.address != first);                       /* hidden: one visible window is the runtime's limit */
    CHECK(r->window.color_set == 0);                                                      /* the second window starts with defaults */
    MacWindowState *a = mac_window_find(r, first);
    CHECK(a && a->color_set == 1 && a->color_rgb[0][0] == 0xffff);                        /* and the first keeps its table */
    mac_cpu_destroy(r->cpu);

    wind(w1, 2, 0, TOP, LEFT, BOTTOM, RIGHT);                                            /* hidden window: stored now, drawn on show */
    r = make(list, 3);
    CHECK(open_window(r, 129) == 1 && !r->window.visible && r->window.color_set == 1);
    uint8_t before = pixel(r, 200, 150);
    r->window.visible = 1; mac_window_draw(r, 1);
    CHECK(pixel(r, 200, 150) == closest(r, 0xffff, 0, 0) && before != pixel(r, 200, 150));
    CHECK(mac_color_set_gray(r, 1) == 0);                                                 /* a palette change is honoured at the next draw */
    mac_window_draw(r, 1);
    uint8_t gray_red = pixel(r, 200, 150);
    CHECK(gray_red == closest(r, 0xffff, 0, 0));
    CHECK(((r->palette[gray_red] >> 16) & 255) == ((r->palette[gray_red] >> 8) & 255));  /* now a gray entry */
    mac_cpu_destroy(r->cpu);
}

int main(void) {
    real_table_on_plain_box();
    distinct_colors_and_titled_windows();
    unused_or_missing_parts_change_nothing();
    rejects_what_it_cannot_draw();
    rejects_trailing_bytes();
    isolation_visibility_and_palette();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("window color table checks ok");
    return 0;
}
