#define _DARWIN_C_SOURCE 1
#include "mac_resource_chain.h"
#include "mac_resfork_write.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The writable Resource Manager: forks a program opens or creates in the save overlay, copy-on-write from the read-only data,
 * the fork writer, and the error codes. Everything is headless: calls are made through mac_resource_service on a stack. */
enum { SP = 0xf000, NAME = 0xe000, NAME2 = 0xe200, TEXT = 0x54455854, STR = 0x53545220, PICT = 0x50494354 };
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

typedef struct { char data[64], saves[64]; MacRuntime rt; } Rig;
static MacRuntime *R;

static void st(void) { mac_cpu_set(R->cpu, MAC_A7, SP); }
static void w16(uint32_t o, unsigned v) { mac_cpu_write(R->cpu, SP + o, 2, v); }
static void w32(uint32_t o, uint32_t v) { mac_cpu_write(R->cpu, SP + o, 4, v); }
static int err(void) { return (int16_t)mac_cpu_read(R->cpu, 0xa60, 2); }
static uint32_t r32(uint32_t a) { return mac_cpu_read(R->cpu, a, 4); }
static void pstr(uint32_t at, const char *s) { uint8_t *m = mac_cpu_ram(R->cpu); m[at] = (uint8_t)strlen(s); memcpy(m + at + 1, s, strlen(s)); }
static void call(uint16_t op) { CHECK(mac_resource_service(R, op) == 1); }

static int open_res(const char *name) { pstr(NAME, name); st(); w32(0, NAME); call(0xa997); return (int16_t)mac_cpu_read(R->cpu, SP + 4, 2); }
static void close_res(int ref) { st(); w16(0, (unsigned)ref); call(0xa99a); }
static void update(int ref) { st(); w16(0, (unsigned)ref); call(0xa999); }
static void use(int ref) { st(); w16(0, (unsigned)ref); call(0xa998); }
static uint32_t get(uint32_t type, int id) { st(); w16(0, (unsigned)id); w32(2, type); call(0xa9a0); return r32(SP + 6); }
static uint32_t new_handle(const char *bytes, uint32_t size) {
    mac_cpu_set(R->cpu, MAC_D0, size); CHECK(mac_memory_service(R, 0xa122) == 1);
    uint32_t h = mac_cpu_get(R->cpu, MAC_A0);
    if (h && size) memcpy(mac_cpu_ram(R->cpu) + r32(h), bytes, size);
    return h;
}
static int add(uint32_t h, uint32_t type, int id, const char *name) {
    if (name) pstr(NAME2, name);
    st(); w32(0, name ? NAME2 : 0); w16(4, (unsigned)id); w32(6, type); w32(10, h); call(0xa9ab);
    CHECK(mac_cpu_get(R->cpu, MAC_A7) == SP + 14);
    return err();
}
static int one(uint16_t op, uint32_t h) { st(); w32(0, h); call(op); CHECK(mac_cpu_get(R->cpu, MAC_A7) == SP + 4); return err(); }
static int attrs_of(uint32_t h) { st(); w32(0, h); call(0xa9a6); return (int16_t)mac_cpu_read(R->cpu, SP + 4, 2); }
static int set_attrs(uint32_t h, int a) { st(); w32(0, 0); w16(0, (unsigned)a); w32(2, h); call(0xa9a7); return err(); }
static int set_info(uint32_t h, int id, const char *name) {
    if (name) pstr(NAME2, name);
    st(); w32(0, name ? NAME2 : 0); w16(4, (unsigned)id); w32(6, h); call(0xa9a9); return err();
}
static void info(uint32_t h, char *name, uint32_t *type, int *id) {
    st(); w32(0, 0xe400); w32(4, 0xe500); w32(8, 0xe510); w32(12, h); call(0xa9a8);
    uint8_t *m = mac_cpu_ram(R->cpu); memcpy(name, m + 0xe401, m[0xe400]); name[m[0xe400]] = 0;
    *type = r32(0xe500); *id = (int16_t)mac_cpu_read(R->cpu, 0xe510, 2);
}
static int file_attr(int ref) { st(); w16(0, (unsigned)ref); call(0xa9f6); return (int16_t)mac_cpu_read(R->cpu, SP + 2, 2); }
static int set_file_attr(int ref, int a) { st(); w16(0, (unsigned)a); w16(2, (unsigned)ref); call(0xa9f7); return err(); }
static int unique(uint16_t op, uint32_t type) { st(); w32(0, type); call(op); return (int16_t)mac_cpu_read(R->cpu, SP + 4, 2); }
static const char *text(uint32_t h, uint32_t n) { (void)n; return (const char *)mac_cpu_ram(R->cpu) + r32(h); }

/* A fork built by the fork writer itself, so the fixtures and the output are the same format. */
static size_t fixture(uint8_t **out, int variant, uint16_t map_attrs) {
    MacWFork w; mac_wfork_init(&w); w.map_attrs = map_attrs;
    uint8_t name[] = {4, 'N', 'a', 'm', 'e'};
    CHECK(mac_wfork_add(&w, TEXT, 128, 0x20, name, (const uint8_t *)(variant ? "orig" : "ORIG"), 4) == 0);
    CHECK(mac_wfork_add(&w, TEXT, 129, 0, NULL, (const uint8_t *)"nine", 4) == 0);
    CHECK(mac_wfork_add(&w, STR, 1, 0x10, NULL, (const uint8_t *)"\3abc", 4) == 0);
    size_t n = 0; CHECK(mac_wfork_build(&w, out, &n) == 0);
    mac_wfork_free(&w);
    return n;
}
static void put_file(const char *dir, const char *name, const uint8_t *b, size_t n) {
    char p[256]; snprintf(p, sizeof p, "%s/%s", dir, name);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0600); CHECK(fd >= 0);
    if (fd >= 0) { CHECK(write(fd, b, n) == (ssize_t)n); close(fd); }
}
static uint8_t *slurp(const char *dir, const char *name, size_t *n) {
    char p[256]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) { *n = 0; return NULL; }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
    uint8_t *b = malloc(*n + 1); CHECK(fread(b, 1, *n, f) == *n); fclose(f);
    return b;
}
static void rm_tree(const char *dir) {
    DIR *d = opendir(dir); struct dirent *e;
    while (d && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (unlink(p)) rm_tree(p);
    }
    if (d) closedir(d);
    rmdir(dir);
}
static int boot(Rig *r, int overlay, unsigned max_files, uint64_t max_bytes) {
    memset(&r->rt, 0, sizeof r->rt); R = &r->rt;
    MacWFork w; mac_wfork_init(&w);   /* the application's own fork: one TEXT, 128 */
    uint8_t *f = NULL; size_t n = 0;
    CHECK(mac_wfork_add(&w, TEXT, 128, 0, NULL, (const uint8_t *)"APP!", 4) == 0 && mac_wfork_build(&w, &f, &n) == 0);
    mac_wfork_free(&w);
    int ok = !mac_resource_open(&r->rt.fork, f, n, NULL, 0);   /* the fork bytes stay alive for the run (leaked, test only) */
    r->rt.cpu = mac_cpu_create(65536); r->rt.heap_next = 0x2000; r->rt.heap_limit = 0xd000;
    return !(ok && r->rt.cpu && !mac_files_root(&r->rt, r->data) && (!overlay || !mac_files_writable(&r->rt, r->saves, max_files, max_bytes)));
}
static void setup_dirs(Rig *r) {
    snprintf(r->data, sizeof r->data, "/tmp/ospi-rw-data-XXXXXX"); snprintf(r->saves, sizeof r->saves, "/tmp/ospi-rw-save-XXXXXX");
    CHECK(mkdtemp(r->data) && mkdtemp(r->saves));
}

static void serialiser(void) {
    MacWFork w; mac_wfork_init(&w);
    CHECK(mac_wfork_add(&w, TEXT, -5, 0x31, (const uint8_t *)"\2hi", (const uint8_t *)"abc", 3) == 0);
    CHECK(mac_wfork_add(&w, PICT, 7, 0, NULL, NULL, 0) == 0);
    CHECK(mac_wfork_add(&w, TEXT, 9, 0, NULL, (const uint8_t *)"z", 1) == 0);
    CHECK(mac_wfork_add(&w, TEXT, 9, 0, NULL, (const uint8_t *)"z", 1) == -194);   /* the pair is taken */
    w.map_attrs = 0x0020;
    uint8_t *b; size_t n; CHECK(mac_wfork_build(&w, &b, &n) == 0);
    MacResourceFork f; CHECK(mac_resource_open(&f, b, n, NULL, 0) == 0 && mac_resource_count(&f) == 3);
    MacResource r;
    CHECK(mac_resource_find(&f, TEXT, -5, &r) == 0 && r.size == 3 && !memcmp(r.data, "abc", 3) && r.attributes == 0x31 && r.name_size == 2 && !memcmp(r.name, "hi", 2));
    CHECK(mac_resource_find(&f, PICT, 7, &r) == 0 && r.size == 0 && !r.name);
    CHECK(mac_resource_at(&f, 1, &r) == 0 && r.type == TEXT && r.id == 9);   /* the types are grouped: both TEXT first */
    CHECK(b[0] == 0 && b[1] == 0 && b[2] == 1 && b[3] == 0);                 /* data at 256 */
    CHECK(f.bytes[f.map_offset + 23] == 0x20);
    /* the same bytes load and build to the same bytes */
    MacWFork w2; CHECK(mac_wfork_load(&w2, &f) == 0 && w2.count == 3);
    uint8_t *b2; size_t n2; CHECK(mac_wfork_build(&w2, &b2, &n2) == 0 && n2 == n && !memcmp(b, b2, n));
    free(b2); mac_wfork_free(&w2);
    /* an empty fork is valid */
    MacWFork e; mac_wfork_init(&e); uint8_t *eb; size_t en;
    CHECK(mac_wfork_build(&e, &eb, &en) == 0 && !mac_resource_open(&f, eb, en, NULL, 0) && mac_resource_count(&f) == 0);
    free(eb); free(b); mac_wfork_free(&w);
    /* a malformed fork does not load */
    uint8_t junk[64] = {0}; MacResourceFork bad;
    CHECK(mac_resource_open(&bad, junk, sizeof junk, NULL, 0) != 0);
}

static void round_trip_and_copy_on_write(void) {
    Rig r; setup_dirs(&r); CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    uint8_t *fork; size_t n = fixture(&fork, 1, 0);
    put_file(r.data, "Plug.rsrc", fork, n);
    int ref = open_res("Plug"); CHECK(ref >= 256 && err() == 0);
    uint32_t h128 = get(TEXT, 128), h1 = get(STR, 1);
    CHECK(h128 && h1 && !memcmp(text(h128, 4), "orig", 4));
    /* the open fork is the data side's; nothing is in the overlay yet */
    size_t sn; uint8_t *s = slurp(r.saves, "Plug.rsrc", &sn); CHECK(!s); free(s);
    memcpy(mac_cpu_ram(R->cpu) + r32(h128), "EDIT", 4);
    CHECK(one(0xa9aa, h128) == 0);                               /* ChangedResource */
    uint32_t add_h = new_handle("hello", 5);
    CHECK(add(add_h, TEXT, 200, "Added") == 0);
    CHECK(add(add_h, TEXT, 201, NULL) == -194);                  /* already a resource's handle */
    CHECK(add(new_handle("x", 1), TEXT, 200, NULL) == -194);     /* the pair is in use */
    CHECK(add(0, TEXT, 202, NULL) == -194);                      /* nil handle */
    CHECK(one(0xa9ad, h1) == 0);                                 /* RmveResource */
    CHECK(one(0xa9ad, h1) == -192);                              /* it is an ordinary handle now */
    CHECK(get(STR, 1) == 0);
    s = slurp(r.saves, "Plug.rsrc", &sn); CHECK(!s);            /* nothing is written before an update or close */
    free(s);
    update(ref); CHECK(err() == 0);
    s = slurp(r.saves, "Plug.rsrc", &sn); CHECK(s && sn > 256);
    size_t dn; uint8_t *d = slurp(r.data, "Plug.rsrc", &dn);
    CHECK(dn == n && !memcmp(d, fork, n));                       /* the read-only data side is byte-identical */
    free(d); free(s);
    close_res(ref); CHECK(err() == 0);
    ref = open_res("Plug"); CHECK(ref >= 256);                  /* the overlay copy is the fork now */
    h128 = get(TEXT, 128);
    CHECK(h128 && !memcmp(text(h128, 4), "EDIT", 4));
    uint32_t ha = get(TEXT, 200); char nm[256]; uint32_t ty; int id;
    CHECK(ha && !memcmp(text(ha, 5), "hello", 5)); info(ha, nm, &ty, &id); CHECK(!strcmp(nm, "Added") && ty == TEXT && id == 200);
    info(h128, nm, &ty, &id); CHECK(!strcmp(nm, "Name"));          /* the name list survived */
    CHECK(get(TEXT, 129) && get(STR, 1) == 0);
    close_res(ref);
    d = slurp(r.data, "Plug.rsrc", &dn); CHECK(dn == n && !memcmp(d, fork, n)); free(d);
    free(fork); mac_runtime_destroy(&r.rt); rm_tree(r.data); rm_tree(r.saves);
}

static void close_flushes_and_changes_follow(void) {
    Rig r; setup_dirs(&r); CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    uint8_t *fork; size_t n = fixture(&fork, 1, 0); put_file(r.data, "C.rsrc", fork, n);
    int ref = open_res("C"); uint32_t h = get(TEXT, 129);
    memcpy(mac_cpu_ram(R->cpu) + r32(h), "ZZZZ", 4); CHECK(one(0xa9aa, h) == 0);
    CHECK(attrs_of(h) & 0x80);                                    /* changed, as the older flag reports it */
    close_res(ref); CHECK(err() == 0);                           /* CloseResFile flushes */
    ref = open_res("C"); h = get(TEXT, 129); CHECK(!memcmp(text(h, 4), "ZZZZ", 4));
    /* WriteResource writes at once */
    memcpy(mac_cpu_ram(R->cpu) + r32(h), "WWWW", 4); CHECK(one(0xa9b0, h) == 0);
    size_t sn; uint8_t *s = slurp(r.saves, "C.rsrc", &sn); MacResourceFork f; MacResource res;
    CHECK(s && !mac_resource_open(&f, s, sn, NULL, 0) && !mac_resource_find(&f, TEXT, 129, &res) && !memcmp(res.data, "WWWW", 4));
    free(s);
    /* reopening the same name finds the open fork (same file) after the copy-on-write */
    CHECK(open_res("C") == ref);
    close_res(ref); free(fork); mac_runtime_destroy(&r.rt); rm_tree(r.data); rm_tree(r.saves);
}

static void names_ids_attributes(void) {
    Rig r; setup_dirs(&r); CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    uint8_t *fork; size_t n = fixture(&fork, 1, 0); put_file(r.data, "A.rsrc", fork, n);
    int ref = open_res("A"); uint32_t h = get(TEXT, 129);
    CHECK(set_info(h, 300, "Renamed") == 0);                      /* a new ID and name */
    CHECK(get(TEXT, 129) == 0 && get(TEXT, 300) == h);
    CHECK(set_info(h, 128, NULL) == -194);                        /* 128 is in use */
    CHECK(set_attrs(h, 0x30) == 0 && attrs_of(h) == 0x30);       /* purgeable and locked */
    CHECK(set_file_attr(ref, 0x20) == 0 && file_attr(ref) == 0x20);
    close_res(ref);
    ref = open_res("A"); h = get(TEXT, 300); char nm[256]; uint32_t ty; int id;
    CHECK(h && attrs_of(h) == 0x30 && file_attr(ref) == 0x20);
    info(h, nm, &ty, &id); CHECK(!strcmp(nm, "Renamed") && id == 300);
    /* a nil name leaves the name; an empty one clears it */
    CHECK(set_info(h, 300, NULL) == 0); info(h, nm, &ty, &id); CHECK(!strcmp(nm, "Renamed"));
    CHECK(set_info(h, 300, "") == 0); info(h, nm, &ty, &id); CHECK(nm[0] == 0);
    /* protected: ChangedResource, WriteResource, SetResInfo and RmveResource refuse; SetResAttrs can lift it */
    CHECK(set_attrs(h, 0x08) == 0);
    CHECK(one(0xa9aa, h) == -198 && one(0xa9b0, h) == -198 && set_info(h, 301, NULL) == -198 && one(0xa9ad, h) == -198);
    CHECK(set_attrs(h, 0) == 0 && one(0xa9aa, h) == 0);
    /* resChanged is not the caller's to set */
    CHECK(set_attrs(h, 0x02) == 0);
    close_res(ref);
    /* bad references */
    update(999); CHECK(err() == -193); CHECK(file_attr(999) == 0 && err() == -193); CHECK(set_file_attr(999, 0) == -193);
    free(fork); mac_runtime_destroy(&r.rt); rm_tree(r.data); rm_tree(r.saves);
}

static void unique_ids(void) {
    Rig r; setup_dirs(&r); CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    uint8_t *fork; size_t n = fixture(&fork, 1, 0); put_file(r.data, "U.rsrc", fork, n);
    int a = open_res("U");
    CHECK(unique(0xa810, TEXT) == 130 && unique(0xa9c1, TEXT) == 130);   /* 128 and 129 are used */
    CHECK(unique(0xa810, PICT) == 128);
    CHECK(add(new_handle("q", 1), TEXT, 130, NULL) == 0);
    CHECK(unique(0xa810, TEXT) == 131);
    uint8_t *f2; size_t n2 = fixture(&f2, 0, 0); put_file(r.data, "V.rsrc", f2, n2);
    int b = open_res("V"); (void)b;
    CHECK(add(new_handle("w", 1), TEXT, 131, NULL) == 0);       /* into V, the current file */
    CHECK(unique(0xa810, TEXT) == 130 && unique(0xa9c1, TEXT) == 132);   /* V alone has 128, 129, 131; every open file together leaves 132 */
    use(a); CHECK(unique(0xa810, TEXT) == 131 && unique(0xa9c1, TEXT) == 132);   /* U holds 128 to 130 */
    free(fork); free(f2); mac_runtime_destroy(&r.rt); rm_tree(r.data); rm_tree(r.saves);
}

static void refusals(void) {
    Rig r; setup_dirs(&r); uint8_t *fork; size_t n = fixture(&fork, 1, 0);
    /* no overlay: a fork in the data can be read, and not written */
    CHECK(boot(&r, 0, 0, 0) == 0); put_file(r.data, "RO.rsrc", fork, n);
    int ref = open_res("RO"); uint32_t h = get(TEXT, 128);
    CHECK(add(new_handle("a", 1), TEXT, 500, NULL) == -44);
    CHECK(one(0xa9ad, h) == -44 && one(0xa9aa, h) == -44 && one(0xa9b0, h) == -44 && set_info(h, 128, "x") == -44 && set_attrs(h, 1) == -44);
    CHECK(set_file_attr(ref, 0) == -44 && get(TEXT, 128) == h);
    close_res(ref); mac_runtime_destroy(&r.rt);
    /* the application's own fork: removing is refused */
    CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    uint32_t ah = mac_runtime_resource(&r.rt, TEXT, 128); CHECK(ah && one(0xa9ad, ah) == -44);
    mac_runtime_destroy(&r.rt);
    /* a fork whose map says read only: every change refuses, and no copy is made */
    uint8_t *ro; size_t rn = fixture(&ro, 1, 0x80); put_file(r.data, "Map.rsrc", ro, rn);
    CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    ref = open_res("Map"); h = get(TEXT, 128);
    CHECK(file_attr(ref) == 0x80 && add(new_handle("a", 1), TEXT, 500, NULL) == -44 && one(0xa9ad, h) == -44 && one(0xa9aa, h) == -44);
    CHECK(set_file_attr(ref, 0) == 0 && add(new_handle("a", 1), TEXT, 500, NULL) == 0);   /* the attribute can be cleared */
    size_t sn; uint8_t *s = slurp(r.saves, "Map.rsrc", &sn); CHECK(!s);
    close_res(ref); mac_runtime_destroy(&r.rt);
    /* a malformed fork refuses to open and nothing is written */
    uint8_t junk[200]; memset(junk, 0x7f, sizeof junk); put_file(r.data, "Bad.rsrc", junk, sizeof junk);
    CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    CHECK(open_res("Bad") == -1 && err() == -199);
    s = slurp(r.saves, "Bad.rsrc", &sn); CHECK(!s);
    mac_runtime_destroy(&r.rt);
    free(fork); free(ro); rm_tree(r.data); rm_tree(r.saves);
}

static void capacity(void) {
    Rig r; setup_dirs(&r); uint8_t *fork; size_t n = fixture(&fork, 1, 0);
    CHECK(boot(&r, 1, 16, 300) == 0);                            /* a byte quota smaller than the fork */
    put_file(r.data, "Big.rsrc", fork, n);
    int ref = open_res("Big"); uint32_t h = get(TEXT, 128);
    CHECK(h && add(new_handle("abc", 3), TEXT, 500, NULL) == 0);   /* the change is held in memory */
    update(ref); CHECK(err() == -34);                            /* the disk is full: nothing lands */
    size_t sn; uint8_t *s = slurp(r.saves, "Big.rsrc", &sn); CHECK(!s);
    close_res(ref); CHECK(err() == -34);                         /* the close is reported, and happens */
    CHECK(open_res("Big") >= 256);                               /* the data side is still there, without the change */
    CHECK(get(TEXT, 500) == 0);
    mac_runtime_destroy(&r.rt);
    /* a file-count quota of zero cannot take a new sidecar */
    CHECK(boot(&r, 1, 1, 1 << 20) == 0);
    int ref2 = open_res("Big"); CHECK(add(new_handle("a", 1), TEXT, 501, NULL) == 0);
    update(ref2); CHECK(err() == 0);                             /* one file fits */
    uint8_t *f2; size_t n2 = fixture(&f2, 0, 0); put_file(r.data, "Two.rsrc", f2, n2);
    int ref3 = open_res("Two"); CHECK(add(new_handle("a", 1), TEXT, 501, NULL) == 0);
    update(ref3); CHECK(err() == -34);                           /* the second does not */
    mac_runtime_destroy(&r.rt); free(fork); free(f2); rm_tree(r.data); rm_tree(r.saves);
}

/* A fork made by CreateResFile in the overlay is writable from the start, and nothing in the data is involved. */
static void created_fork(void) {
    Rig r; setup_dirs(&r); CHECK(boot(&r, 1, 16, 1 << 20) == 0);
    pstr(NAME, "Fresh");
    st(); w32(0, NAME); CHECK(mac_files_crf_service(R, 0xa9b1) == 1 && err() == 0);
    int ref = open_res("Fresh"); CHECK(ref >= 256 && err() == 0);
    CHECK(get(TEXT, 128) != 0 && 1);   /* the application's TEXT 128 answers behind the empty file */
    CHECK(add(new_handle("fresh", 5), PICT, 3, "Pic") == 0);
    CHECK(set_file_attr(ref, 0x20) == 0);
    close_res(ref); CHECK(err() == 0);
    size_t sn; uint8_t *s = slurp(r.saves, "Fresh.rsrc", &sn); CHECK(s && sn > 286); free(s);
    ref = open_res("Fresh"); uint32_t h = get(PICT, 3); char nm[256]; uint32_t ty; int id;
    CHECK(h && !memcmp(text(h, 5), "fresh", 5) && file_attr(ref) == 0x20);
    info(h, nm, &ty, &id); CHECK(!strcmp(nm, "Pic"));
    close_res(ref); mac_runtime_destroy(&r.rt); rm_tree(r.data); rm_tree(r.saves);
}

int main(void) {
    created_fork();
    serialiser();
    round_trip_and_copy_on_write();
    close_flushes_and_changes_follow();
    names_ids_attributes();
    unique_ids();
    refusals();
    capacity();
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    puts("mac_resource_write ok");
    return 0;
}
