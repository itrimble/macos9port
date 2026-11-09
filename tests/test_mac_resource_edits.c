#include "mac_runtime.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Persistence of WriteResource/SetResInfo edits through the save overlay (hidden, fork-bound file). */
enum { DATA = 256, MAP = 264, TYPES = MAP + 28, REF = TYPES + 10, NAME = REF + 12, END = NAME + 5,
       SP = 0xf000, TEXT = 0x54455854 };
static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void put16(uint8_t *p, uint16_t x) { p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x) { put16(p, (uint16_t)(x >> 16)); put16(p + 2, (uint16_t)x); }

typedef struct { char data[64], saves[64]; uint8_t fork[END]; MacRuntime rt; } Rig;

static void build_fork(uint8_t *p, char variant) {
    memset(p, 0, END);
    put32(p, DATA); put32(p + 4, MAP); put32(p + 8, 8); put32(p + 12, END - MAP);
    put32(p + DATA, 4); memcpy(p + DATA + 4, "Tes", 3); p[DATA + 7] = (uint8_t)variant;
    put16(p + MAP + 24, 28); put16(p + MAP + 26, NAME - MAP);
    put16(p + TYPES, 0); put32(p + TYPES + 2, TEXT);
    put16(p + TYPES + 6, 0); put16(p + TYPES + 8, 10);
    put16(p + REF, 128); put16(p + REF + 2, 0); p[REF + 4] = 0x20;
    p[NAME] = 4; memcpy(p + NAME + 1, "Name", 4);
}

static int open_rt(Rig *r, char variant, unsigned max_files, uint64_t max_bytes, int overlay) {
    memset(&r->rt, 0, sizeof r->rt);
    build_fork(r->fork, variant);
    if (mac_resource_open(&r->rt.fork, r->fork, END, NULL, 0)) return -1;
    r->rt.cpu = mac_cpu_create(65536);
    if (!r->rt.cpu) return -1;
    r->rt.heap_next = 0x2000; r->rt.heap_limit = 0x8000;
    if (mac_files_root(&r->rt, r->data)) return -1;
    if (overlay && mac_files_writable(&r->rt, r->saves, max_files, max_bytes)) return -1;
    return 0;
}

static void close_rt(Rig *r) { mac_runtime_destroy(&r->rt); }
static void word(MacRuntime *rt, uint32_t off, uint32_t v) { mac_cpu_write(rt->cpu, SP + off, 2, v); }
static void lng(MacRuntime *rt, uint32_t off, uint32_t v) { mac_cpu_write(rt->cpu, SP + off, 4, v); }
static int err(MacRuntime *rt) { return (int16_t)mac_cpu_read(rt->cpu, 0xa60, 2); }
static uint32_t get_res(MacRuntime *rt) {
    mac_cpu_set(rt->cpu, MAC_A7, SP); word(rt, 0, 128); lng(rt, 2, TEXT); lng(rt, 6, 0);
    CHECK(mac_resource_service(rt, 0xa9a0) == 1);
    return mac_cpu_read(rt->cpu, SP + 6, 4);
}
static void one(MacRuntime *rt, uint16_t op, uint32_t h) {
    mac_cpu_set(rt->cpu, MAC_A7, SP); lng(rt, 0, h); lng(rt, 4, 0);
    CHECK(mac_resource_service(rt, op) == 1);
}
static void set_name(MacRuntime *rt, uint32_t h, const char *name) {
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    ram[0x1000] = (uint8_t)strlen(name); memcpy(ram + 0x1001, name, strlen(name));
    mac_cpu_set(rt->cpu, MAC_A7, SP); lng(rt, 0, 0x1000); word(rt, 4, 128); lng(rt, 6, h);
    CHECK(mac_resource_service(rt, 0xa9a9) == 1);
}
static void info_name(MacRuntime *rt, uint32_t h, char *out) {
    mac_cpu_set(rt->cpu, MAC_A7, SP); lng(rt, 0, 0x1200); lng(rt, 4, 0x1300); lng(rt, 8, 0x1310); lng(rt, 12, h);
    CHECK(mac_resource_service(rt, 0xa9a8) == 1);
    uint8_t *ram = mac_cpu_ram(rt->cpu);
    memcpy(out, ram + 0x1201, ram[0x1200]); out[ram[0x1200]] = 0;
}
static const uint8_t *body(MacRuntime *rt, uint32_t h) { return mac_cpu_ram(rt->cpu) + mac_cpu_read(rt->cpu, h, 4); }

static void rm_tree(const char *dir) {
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char path[512]; snprintf(path, sizeof path, "%s/%s", dir, e->d_name); unlink(path);
        }
        closedir(d);
    }
    rmdir(dir);
}

static void edit_and_write(Rig *r, const char *text, const char *rename) {
    uint32_t h = get_res(&r->rt);
    memcpy((uint8_t *)body(&r->rt, h), text, 4);
    if (rename) set_name(&r->rt, h, rename);
    one(&r->rt, 0xa9b0, h);
}

static int hidden_files(Rig *r, int *stage) {
    int n = 0; *stage = 0;
    DIR *d = opendir(r->saves); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strncmp(e->d_name, ".resedits-", 10)) n++;
        if (!strncmp(e->d_name, ".stage-", 7)) (*stage)++;
    }
    if (d) closedir(d);
    return n;
}

static void file_path(Rig *r, char *out, size_t n) {
    DIR *d = opendir(r->saves); struct dirent *e; out[0] = 0;
    while (d && (e = readdir(d))) if (!strncmp(e->d_name, ".resedits-", 10)) snprintf(out, n, "%s/%s", r->saves, e->d_name);
    if (d) closedir(d);
}

static void persists_across_runs(Rig *r) {
    CHECK(open_rt(r, 'T', 8, 1 << 20, 1) == 0);
    edit_and_write(r, "Edit", "oz");
    CHECK(err(&r->rt) == 0);
    int stage; CHECK(hidden_files(r, &stage) == 1 && stage == 0);
    close_rt(r);

    CHECK(open_rt(r, 'T', 8, 1 << 20, 1) == 0);                     /* a new run of the same program */
    CHECK(mac_resedit_load(&r->rt) == 1);
    uint32_t h = get_res(&r->rt);
    CHECK(h && memcmp(body(&r->rt, h), "Edit", 4) == 0);
    char name[256]; info_name(&r->rt, h, name); CHECK(!strcmp(name, "oz"));
    close_rt(r);

    CHECK(open_rt(r, 'T', 8, 1 << 20, 1) == 0);                     /* edits are not applied unless loaded */
    h = get_res(&r->rt); CHECK(memcmp(body(&r->rt, h), "TesT", 4) == 0);
    close_rt(r);

    CHECK(open_rt(r, 'X', 8, 1 << 20, 1) == 0);                     /* a different build of the program */
    CHECK(mac_resedit_load(&r->rt) == 0);
    h = get_res(&r->rt); CHECK(memcmp(body(&r->rt, h), "TesX", 4) == 0);
    close_rt(r);
}

static void rejects_corrupt_files(Rig *r) {
    char path[512]; file_path(r, path, sizeof path); CHECK(path[0]);
    FILE *f = fopen(path, "rb"); uint8_t good[4096]; size_t n = f ? fread(good, 1, sizeof good, f) : 0; if (f) fclose(f);
    CHECK(n > 12);
    for (int variant = 0; variant < 9; variant++) {
        uint8_t bad[4096]; size_t len = n; memcpy(bad, good, n);
        if (variant == 0) len = n - 1;                              /* truncated */
        if (variant == 1) bad[0] ^= 0xff;                           /* bad magic */
        if (variant == 2) { bad[8] = 0x7f; }                        /* absurd entry count */
        if (variant == 3) bad[8 + 4 + 6] = 0xfc;                    /* undefined flag bits */
        if (variant == 4) { bad[len++] = 0; }                       /* trailing garbage */
        if (variant == 5) len = 12;                                 /* header only */
        if (variant == 6) { bad[8 + 4 + 7] = 0xff; }                /* name length runs past the end */
        if (variant == 7) { bad[8 + 4 + 7 + 3 + 3] = 0x7f; }        /* data length far beyond the file */
        if (variant == 8) len = 0;                                  /* empty */
        f = fopen(path, "wb"); fwrite(bad, 1, len, f); fclose(f);
        CHECK(open_rt(r, 'T', 8, 1 << 20, 1) == 0);
        int applied = mac_resedit_load(&r->rt);
        uint32_t h = get_res(&r->rt);
        CHECK(applied == 0 && h && memcmp(body(&r->rt, h), "TesT", 4) == 0);   /* nothing partially applied */
        close_rt(r);
    }
}

static void quota_and_modes(Rig *r) {
    CHECK(open_rt(r, 'T', 8, 64, 1) == 0);                          /* quota/4 = 16 bytes: too small to save */
    edit_and_write(r, "Edit", NULL);
    CHECK(err(&r->rt) == -34);                                      /* dskFulErr is reported... */
    uint32_t h = get_res(&r->rt);
    one(&r->rt, 0xa9a3, h);
    h = get_res(&r->rt); CHECK(memcmp(body(&r->rt, h), "Edit", 4) == 0);   /* ...but the session change stands */
    int stage; CHECK(hidden_files(r, &stage) == 1 && stage == 0);          /* the earlier file is untouched, no debris */
    close_rt(r);

    CHECK(open_rt(r, 'T', 8, 1 << 20, 0) == 0);                     /* read-only volume: session only */
    edit_and_write(r, "Edit", NULL);
    CHECK(err(&r->rt) == 0);
    CHECK(mac_resedit_save(&r->rt) == 1);                           /* reported as "not persisted" */
    h = get_res(&r->rt); CHECK(memcmp(body(&r->rt, h), "Edit", 4) == 0);
    close_rt(r);

    CHECK(open_rt(r, 'T', 8, 1 << 20, 1) == 0);                     /* name validation on the hidden-file API */
    const uint8_t x[1] = {0};
    CHECK(mac_files_overlay_put_hidden(&r->rt, "../escape", x, 1) == -50);
    CHECK(mac_files_overlay_put_hidden(&r->rt, "visible", x, 1) == -50);
    CHECK(mac_files_overlay_put_hidden(&r->rt, ".resedits-ZZZZZZZZZZZZZZZZ", x, 1) == -50);
    CHECK(mac_files_overlay_put_hidden(&r->rt, ".resedits-0123456789abcdef/x", x, 1) == -50);
    uint8_t *data = NULL; size_t size = 0;
    CHECK(mac_files_overlay_get_hidden(&r->rt, "visible", 100, &data, &size) != 0);
    CHECK(mac_files_overlay_get_hidden(&r->rt, ".resedits-0123456789abcdef", 100, &data, &size) != 0);   /* absent */
    close_rt(r);
}

int main(void) {
    Rig r;
    snprintf(r.data, sizeof r.data, "/tmp/os999-redit-d-XXXXXX");
    snprintf(r.saves, sizeof r.saves, "/tmp/os999-redit-s-XXXXXX");
    if (!mkdtemp(r.data) || !mkdtemp(r.saves)) { fprintf(stderr, "no temp dirs\n"); return 2; }
    persists_across_runs(&r);
    rejects_corrupt_files(&r);
    quota_and_modes(&r);
    rm_tree(r.data); rm_tree(r.saves);
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("resource edit persistence checks ok");
    return 0;
}
