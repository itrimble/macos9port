#include "mac_menu_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ClearMenuBar ($A934), GetNewMBar ($A9C0), SetMenuBar ($A93C), submenus (command byte 27 + InsertMenu(menu,-1)),
 * on a bare runtime and with the protected heap attached (the menu list is then a heap handle that can move). */
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define SP 0x8000u
#define MENU 0x4d454e55u
#define MBAR 0x4d424152u

typedef struct { uint32_t type; int16_t id; const uint8_t *data; size_t size; } Res;
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v >> 16); put16(p + 2, v & 0xffff); }

/* One resource per type group is enough: build a valid fork holding every resource in `list`. */
static size_t build_fork(uint8_t *out, const Res *list, unsigned n) {
    size_t at = 256, data_len = 0;
    uint32_t off[16];
    for (unsigned i = 0; i < n; i++) {
        off[i] = (uint32_t)data_len; put32(out + at + data_len, (uint32_t)list[i].size);
        memcpy(out + at + data_len + 4, list[i].data, list[i].size); data_len += 4 + list[i].size;
    }
    size_t map = at + data_len;
    uint32_t types[16]; unsigned nt = 0;
    for (unsigned i = 0; i < n; i++) { unsigned k = 0; while (k < nt && types[k] != list[i].type) k++; if (k == nt) types[nt++] = list[i].type; }
    size_t tl = map + 28, refs = tl + 2 + 8 * nt, len = (refs - map) + 12 * n + 2;
    memset(out + map, 0, len);
    put32(out, 256); put32(out + 4, (uint32_t)map); put32(out + 8, (uint32_t)data_len); put32(out + 12, (uint32_t)len);
    memcpy(out + map, out, 16); put16(out + map + 24, 28); put16(out + map + 26, (unsigned)(len - 2));
    put16(out + tl, nt - 1);
    unsigned ref = 0;
    for (unsigned t = 0; t < nt; t++) {
        unsigned count = 0; for (unsigned i = 0; i < n; i++) count += list[i].type == types[t];
        put32(out + tl + 2 + 8 * t, types[t]); put16(out + tl + 6 + 8 * t, count - 1);
        put16(out + tl + 8 + 8 * t, (unsigned)(refs - tl + 12 * ref));
        for (unsigned i = 0; i < n; i++) if (list[i].type == types[t]) {
            uint8_t *r = out + refs + 12 * ref++;
            put16(r, (uint16_t)list[i].id); put16(r + 2, 0xffff); put32(r + 4, off[i]);
        }
    }
    return map + len;
}

/* MENU: id, width, height, mdef(4)=0, flags, title, items (name, icon, command, mark, style) */
static size_t menu_res(uint8_t *b, int id, const char *title, unsigned items, const unsigned *icon, const unsigned *cmd, const unsigned *mark) {
    memset(b, 0, 64); put16(b, (unsigned)id); memset(b + 10, 0xff, 4);
    size_t p = 14; b[p++] = (uint8_t)strlen(title); memcpy(b + p, title, strlen(title)); p += strlen(title);
    for (unsigned i = 0; i < items; i++) {
        b[p++] = 4; memcpy(b + p, "Item", 4); p += 4; b[p++] = icon ? icon[i] : 0; b[p++] = cmd ? cmd[i] : 0; b[p++] = mark ? mark[i] : 0; b[p++] = 0;
    }
    b[p++] = 0; return p;
}

static void wr(MacRuntime *r, uint32_t p, unsigned n, uint32_t v) { CHECK(!mac_cpu_write(r->cpu, p, n, v)); }
static uint32_t rd(MacRuntime *r, uint32_t p, unsigned n) { return mac_cpu_read(r->cpu, p, n); }
static int call(MacRuntime *r, unsigned op) { mac_cpu_set(r->cpu, MAC_A7, SP); return mac_menu_service(r, (uint16_t)op); }
static int16_t res_error(MacRuntime *r) { return (int16_t)rd(r, 0xa60, 2); }

static uint8_t fork_bytes[4096];
static int setup(MacRuntime *r, int with_heap, int init_menus) {
    memset(r, 0, sizeof *r);
    uint8_t m128[64], m129[64], m130[64], m131[64], m133[64], mi[64], m134[64];
    static const unsigned sub[] = {0, 27, 0}, subm[] = {0, 133, 0}, icon[] = {1};
    size_t n128 = menu_res(m128, 128, "\x14", 1, NULL, NULL, NULL), n129 = menu_res(m129, 129, "File", 2, NULL, NULL, NULL);
    size_t n130 = menu_res(m130, 130, "Edit", 1, NULL, NULL, NULL), n131 = menu_res(m131, 131, "Special", 3, NULL, sub, subm);
    size_t n133 = menu_res(m133, 133, "Controls", 2, NULL, NULL, NULL), nic = menu_res(mi, 140, "Icons", 1, icon, NULL, NULL);
    static const unsigned c28[] = {28};
    size_t n134 = menu_res(m134, 134, "Cmd28", 1, NULL, c28, NULL);
    static const uint8_t bar128[] = {0, 2, 0, 129, 0, 130}, bar129[] = {0, 0}, bar130[] = {0, 2, 0, 129, 0, 129},
        bar131[] = {0, 2, 0, 129, 3, 231}, bar132[] = {0, 3, 0, 129}, bar133[] = {0, 33};
    uint8_t bar134[2 + 66]; memset(bar134, 0, sizeof bar134); put16(bar134, 33);
    const Res list[] = {{MENU, 128, m128, n128}, {MENU, 129, m129, n129}, {MENU, 130, m130, n130}, {MENU, 131, m131, n131},
        {MENU, 133, m133, n133}, {MENU, 140, mi, nic}, {MENU, 134, m134, n134},
        {MBAR, 128, bar128, sizeof bar128}, {MBAR, 129, bar129, sizeof bar129}, {MBAR, 130, bar130, sizeof bar130},
        {MBAR, 131, bar131, sizeof bar131}, {MBAR, 132, bar132, sizeof bar132}, {MBAR, 133, bar133, sizeof bar133}, {MBAR, 134, bar134, sizeof bar134}};
    size_t size = build_fork(fork_bytes, list, 14);
    char err[128];
    if (mac_resource_open(&r->fork, fork_bytes, size, err, sizeof err)) { fprintf(stderr, "fork: %s\n", err); return -1; }
    r->cpu = mac_cpu_create(MAC_RUNTIME_RAM);
    if (!r->cpu) return -1;
    r->heap_next = 0x10000; r->heap_limit = 0x20000;
    r->screen_base = 0xe00000; r->screen_width = 640; r->screen_height = 480; r->screen_row_bytes = 640; r->screen_depth = 8;
    r->palette[255] = 0xffffff;
    if (with_heap) { r->heap_limit = 0x400000; if (mac_heap_attach(r, 0x400000, 0x700000)) return -1; }
    return init_menus && mac_toolbox_service(r, 0xa930) != 1 ? -1 : 0;
}

static uint32_t get_menu(MacRuntime *r, int id) { wr(r, SP, 2, (uint16_t)id); CHECK(call(r, 0xa9bf) == 1); return rd(r, SP + 2, 4); }
static uint32_t get_mbar(MacRuntime *r, int id) {
    wr(r, SP, 2, (uint16_t)id); wr(r, SP + 2, 4, 0xdeadbeef);
    CHECK(call(r, 0xa9c0) == 1 && mac_cpu_get(r->cpu, MAC_A7) == SP + 2);
    return rd(r, SP + 2, 4);
}
static void insert(MacRuntime *r, uint32_t h, int before) { wr(r, SP, 2, (uint16_t)before); wr(r, SP + 2, 4, h); CHECK(call(r, 0xa935) == 1); }
static unsigned installed(MacRuntime *r) { unsigned n = 99; return mac_menu_list(r, &n) ? n : 99; }
static uint32_t installed_at(MacRuntime *r, unsigned i) { MacHandle *l = mac_menu_list(r, NULL); return l ? rd(r, l->pointer + 2 + 4 * i, 4) : 0; }
static uint32_t new_handle(MacRuntime *r, uint32_t size) { mac_cpu_set(r->cpu, MAC_D0, size); CHECK(mac_memory_service(r, 0xa122) == 1); return mac_cpu_get(r->cpu, MAC_A0); }

static void clear_menu_bar(int heap) {
    MacRuntime r; CHECK(setup(&r, heap, 1) == 0);
    uint32_t a = get_menu(&r, 129), b = get_menu(&r, 130), c = get_menu(&r, 131);
    insert(&r, a, 0); insert(&r, b, 0); insert(&r, c, 0);
    CHECK(installed(&r) == 3);
    CHECK(call(&r, 0xa934) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP);   /* pops nothing */
    CHECK(installed(&r) == 0 && mac_menu_list(&r, NULL)->size == 2);
    CHECK(mac_menu_record(&r, a, NULL) && mac_menu_record(&r, b, NULL) && mac_menu_record(&r, c, NULL));   /* menus survive */
    CHECK(call(&r, 0xa934) == 1 && installed(&r) == 0);                    /* idempotent */
    CHECK(call(&r, 0xa937) == 1);                                          /* DrawMenuBar on an empty bar */
    insert(&r, b, 0); CHECK(installed(&r) == 1 && installed_at(&r, 0) == b);   /* and it is reusable */
    MacHandle *l = mac_menu_list(&r, NULL);
    mac_cpu_set(r.cpu, MAC_A0, l->handle); CHECK(mac_memory_service(&r, 0xa029) == 1);   /* locked list */
    CHECK(call(&r, 0xa934) == 1 && (int16_t)rd(&r, 0x220, 2) == -117 && installed(&r) == 1);   /* refuses, unchanged */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 0) == 0);                                        /* before InitMenus */
    CHECK(call(&r, 0xa934) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
}

static void get_new_mbar(int heap) {
    MacRuntime r; CHECK(setup(&r, heap, 1) == 0);
    uint32_t l1 = get_mbar(&r, 128);
    unsigned n = 0;
    CHECK(l1 && res_error(&r) == 0);
    uint32_t p = rd(&r, l1, 4);
    CHECK(rd(&r, p, 2) == 2);
    uint32_t m129 = rd(&r, p + 2, 4), m130 = rd(&r, p + 6, 4);
    MacHandle *r129 = mac_menu_record(&r, m129, &n), *r130 = mac_menu_record(&r, m130, NULL);
    CHECK(r129 && n == 2 && r130 && rd(&r, r129->pointer, 2) == 129 && rd(&r, r130->pointer, 2) == 130);   /* in MBAR order */
    CHECK(installed(&r) == 0);                                             /* not installed by GetNewMBar */
    uint32_t l2 = get_mbar(&r, 128);
    CHECK(l2 && l2 != l1 && rd(&r, rd(&r, l2, 4) + 2, 4) == m129);         /* a fresh list, the same loaded menus */
    CHECK(rd(&r, rd(&r, get_mbar(&r, 130), 4), 2) == 1);                    /* duplicate IDs collapse */
    uint32_t empty = get_mbar(&r, 129); CHECK(empty && rd(&r, rd(&r, empty, 4), 2) == 0);
    CHECK(get_mbar(&r, 999) == 0 && res_error(&r) == -192);                  /* no such MBAR */
    CHECK(get_mbar(&r, 131) == 0 && res_error(&r) == -192);                  /* refers to a missing MENU */
    CHECK(get_mbar(&r, 132) == 0 && res_error(&r) == -199);                  /* truncated: count says 3, one ID present */
    CHECK(!mac_cpu_halted(r.cpu));
    CHECK(get_mbar(&r, 133) == 0 && res_error(&r) == -199);                  /* declares 33 menus but is truncated */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);
    wr(&r, SP, 2, 134); CHECK(call(&r, 0xa9c0) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
    /* The result is an ordinary handle the program may dispose. */
    CHECK(setup(&r, heap, 1) == 0);
    uint32_t h = get_mbar(&r, 128); mac_cpu_set(r.cpu, MAC_A0, h);
    CHECK(mac_memory_service(&r, 0xa023) == 1 && (int16_t)rd(&r, 0x220, 2) == 0 && rd(&r, h, 4) == 0);
    mac_cpu_destroy(r.cpu);
}

static void set_menu_bar(int heap) {
    MacRuntime r; CHECK(setup(&r, heap, 1) == 0);
    uint32_t keep = get_menu(&r, 131); insert(&r, keep, 0);
    uint32_t src = get_mbar(&r, 128);
    wr(&r, SP, 4, src); CHECK(call(&r, 0xa93c) == 1 && mac_cpu_get(r.cpu, MAC_A7) == SP + 4);
    CHECK(installed(&r) == 2 && rd(&r, mac_menu_list(&r, NULL)->pointer + 2, 4) == rd(&r, rd(&r, src, 4) + 2, 4));   /* replaced, in order */
    mac_cpu_set(r.cpu, MAC_A0, src); CHECK(mac_memory_service(&r, 0xa023) == 1);      /* caller disposes its handle... */
    CHECK(installed(&r) == 2 && call(&r, 0xa937) == 1);                               /* ...the bar is independent */
    uint32_t bar = r.menu_bar; wr(&r, SP, 4, bar);
    CHECK(call(&r, 0xa93c) == 1 && installed(&r) == 2);                               /* installing itself is a no-op */
    wr(&r, SP, 4, get_mbar(&r, 129)); CHECK(call(&r, 0xa93c) == 1 && installed(&r) == 0);   /* an empty list clears the bar */
    wr(&r, SP, 4, 0x1234); CHECK(call(&r, 0xa93c) == -1 && mac_cpu_halted(r.cpu));    /* not a menu list */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);
    uint32_t m = get_menu(&r, 129); wr(&r, SP, 4, m); CHECK(call(&r, 0xa93c) == -1 && mac_cpu_halted(r.cpu));   /* a menu is not a list */
    mac_cpu_destroy(r.cpu);
}

static void submenus(int heap) {
    MacRuntime r; CHECK(setup(&r, heap, 1) == 0);
    uint32_t special = get_menu(&r, 131), controls = get_menu(&r, 133);
    CHECK(special && controls);                                            /* a command-27 item loads */
    insert(&r, special, 0);
    insert(&r, controls, -1);                                              /* registered as a submenu */
    CHECK(installed(&r) == 1 && r.hier_count == 1);                          /* not in the bar */
    insert(&r, controls, -1); CHECK(r.hier_count == 1);                     /* duplicate: no change */
    wr(&r, SP, 2, 133); CHECK(call(&r, 0xa949) == 1 && rd(&r, SP + 2, 4) == controls);   /* GetMHandle finds it */
    wr(&r, SP, 2, 131); CHECK(call(&r, 0xa949) == 1 && rd(&r, SP + 2, 4) == special);
    CHECK(call(&r, 0xa937) == 1);
    wr(&r, SP, 4, controls); CHECK(call(&r, 0xa932) == -1 && mac_cpu_halted(r.cpu));    /* DisposeMenu before DeleteMenu */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);
    controls = get_menu(&r, 133); insert(&r, controls, -1);
    wr(&r, SP, 2, 133); CHECK(call(&r, 0xa936) == 1 && r.hier_count == 0);   /* DeleteMenu removes it */
    wr(&r, SP, 2, 133); CHECK(call(&r, 0xa949) == 1 && rd(&r, SP + 2, 4) == 0);
    wr(&r, SP, 4, controls); CHECK(call(&r, 0xa932) == 1);                  /* now disposable */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);
    for (unsigned i = 0; i < 32; i++) {                                    /* capacity is bounded and loud */
        char t[8]; snprintf(t, sizeof t, "M%u", i);
        mac_cpu_ram(r.cpu)[0x4000] = (uint8_t)strlen(t); memcpy(mac_cpu_ram(r.cpu) + 0x4001, t, strlen(t));
        wr(&r, SP, 4, 0x4000); wr(&r, SP + 4, 2, 300 + i); CHECK(call(&r, 0xa931) == 1);
        insert(&r, rd(&r, SP + 6, 4), -1);
    }
    CHECK(r.hier_count == 32);
    wr(&r, SP, 4, 0x4000); wr(&r, SP + 4, 2, 999); CHECK(call(&r, 0xa931) == 1);
    wr(&r, SP, 2, (uint16_t)-1); wr(&r, SP + 2, 4, rd(&r, SP + 6, 4)); CHECK(call(&r, 0xa935) == -1 && mac_cpu_halted(r.cpu));
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);                                        /* unsupported items are still refused */
    wr(&r, SP, 2, 140); CHECK(call(&r, 0xa9bf) == -1 && mac_cpu_halted(r.cpu));   /* icon */
    mac_cpu_destroy(r.cpu);
    CHECK(setup(&r, heap, 1) == 0);
    wr(&r, SP, 2, 134); CHECK(call(&r, 0xa9bf) == -1 && mac_cpu_halted(r.cpu));   /* command 28 */
    mac_cpu_destroy(r.cpu);
}

/* The menu list is a protected-heap handle: growing it relocates it, and compaction moves it. Both must leave the
 * guest's master pointer, the handle table and the heap in agreement. */
static void list_survives_heap_moves(void) {
    MacRuntime r; CHECK(setup(&r, 1, 1) == 0);
    MacHandle *l = mac_menu_list(&r, NULL); CHECK(l && mac_heap_owns(&r, l->handle));
    uint32_t pad = new_handle(&r, 4000), fence = new_handle(&r, 64);          /* pad sits before whatever the list grows into */
    CHECK(pad && fence);
    for (unsigned i = 0; i < 20; i++) {
        mac_cpu_ram(r.cpu)[0x4000] = 1; mac_cpu_ram(r.cpu)[0x4001] = (uint8_t)('A' + i);   /* one-character titles fit the bar */
        wr(&r, SP, 4, 0x4000); wr(&r, SP + 4, 2, 400 + i); CHECK(call(&r, 0xa931) == 1);
        insert(&r, rd(&r, SP + 6, 4), 0);
    }
    l = mac_menu_list(&r, NULL);
    CHECK(installed(&r) == 20 && l->pointer == rd(&r, l->handle, 4) && l->size == 82);       /* record == master == size */
    uint32_t order[20]; for (unsigned i = 0; i < 20; i++) order[i] = installed_at(&r, i);
    mac_cpu_set(r.cpu, MAC_A0, pad); CHECK(mac_memory_service(&r, 0xa023) == 1);   /* free space below the list */
    mac_cpu_set(r.cpu, MAC_D0, 0); CHECK(mac_memory_service(&r, 0xa04c) == 1);                     /* CompactMem: moves the list */
    l = mac_menu_list(&r, NULL);
    CHECK(l && installed(&r) == 20 && l->pointer == rd(&r, l->handle, 4));                          /* still consistent */
    for (unsigned i = 0; i < 20; i++) CHECK(installed_at(&r, i) == order[i]);                       /* contents intact */
    CHECK(call(&r, 0xa937) == 1);
    CHECK(call(&r, 0xa934) == 1 && installed(&r) == 0 && l->pointer == rd(&r, l->handle, 4));
    mac_cpu_destroy(r.cpu);
}

int main(void) {
    for (int heap = 0; heap < 2; heap++) { clear_menu_bar(heap); get_new_mbar(heap); set_menu_bar(heap); submenus(heap); }
    list_survives_heap_moves();
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("menu bar checks ok");
    return 0;
}
