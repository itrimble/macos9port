/* Headless checks for expanding a local archive (L06): the confirm dialog, the
 * new-folder rule, traversal/symlink refusal, the capacity check, corrupt and
 * encrypted archives, name clashes, and that nothing extracted is run. The
 * archives are built here; no third-party archive is used. */
#include "ospi.h"
#include "../src/session_int.h"
#include "os999_storage.h"
#include "volume_capacity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

typedef struct { uint8_t *p; size_t n, cap; uint8_t *cd; size_t cdn, cdcap; uint32_t entries; } Zip;

static void put(uint8_t **p, size_t *n, size_t *cap, const void *d, size_t k) {
    if (*n + k > *cap) { *cap = (*n + k) * 2; *p = realloc(*p, *cap); }
    memcpy(*p + *n, d, k);
    *n += k;
}
static void le16(uint8_t *o, unsigned v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *o, uint32_t v) { le16(o, v & 0xffff); le16(o + 2, v >> 16); }

/* One stored entry. `mode` is the unix mode (0 for none); `flags` bit 0 marks it encrypted;
 * `bad_crc` stores a wrong checksum. */
static void zip_add(Zip *z, const char *name, const void *data, size_t len, uint32_t mode, unsigned flags, int bad_crc) {
    uint8_t h[30], c[46];
    uint32_t crc = st_crc32(0, data, len) ^ (bad_crc ? 0x5a5a5a5au : 0);
    size_t nl = strlen(name);
    memset(h, 0, sizeof h); memset(c, 0, sizeof c);
    le32(h, 0x04034b50u); le16(h + 4, 20); le16(h + 6, flags); le32(h + 14, crc);
    le32(h + 18, (uint32_t)len); le32(h + 22, (uint32_t)len); le16(h + 26, (unsigned)nl);
    le32(c, 0x02014b50u); le16(c + 4, mode ? 0x0314 : 20); le16(c + 6, 20); le16(c + 8, flags);
    le32(c + 16, crc); le32(c + 20, (uint32_t)len); le32(c + 24, (uint32_t)len); le16(c + 28, (unsigned)nl);
    le32(c + 38, mode << 16); le32(c + 42, (uint32_t)z->n);
    put(&z->p, &z->n, &z->cap, h, sizeof h); put(&z->p, &z->n, &z->cap, name, nl); put(&z->p, &z->n, &z->cap, data, len);
    put(&z->cd, &z->cdn, &z->cdcap, c, sizeof c); put(&z->cd, &z->cdn, &z->cdcap, name, nl);
    z->entries++;
}
static int zip_write(Zip *z, const char *path) {
    uint8_t e[22] = {0};
    uint32_t start = (uint32_t)z->n;
    put(&z->p, &z->n, &z->cap, z->cd, z->cdn);
    le32(e, 0x06054b50u); le16(e + 8, z->entries); le16(e + 10, z->entries);
    le32(e + 12, (uint32_t)z->cdn); le32(e + 16, start);
    put(&z->p, &z->n, &z->cap, e, sizeof e);
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(z->p, 1, z->n, f) == z->n;
    if (f) ok = fclose(f) == 0 && ok;
    free(z->p); free(z->cd);
    memset(z, 0, sizeof *z);
    return ok ? 0 : -1;
}
static int write_bytes(const char *path, const void *d, size_t n) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = !n || fwrite(d, 1, n, f) == n;
    return fclose(f) == 0 && ok ? 0 : -1;
}
static int exists(const char *a, const char *b) {
    char p[OSPI_PATH * 2];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", a, b);
    return lstat(p, &st) == 0;
}
static int dir_entries(const char *root, const char *rel) {
    char p[OSPI_PATH * 2];
    int n = 0;
    struct dirent *de;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    DIR *d = opendir(p);
    while (d && (de = readdir(d))) if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) n++;
    if (d) closedir(d);
    return n;
}
static int find_win(const Session *s, const char *title) {
    for (int i = 0; i < s->nwin; i++) if (strcmp(s->win[i].title, title) == 0) return i;
    return -1;
}
static void vpath(char *out, size_t n, const char *root, const char *rel) { snprintf(out, n, "%s/%s", root, rel); }

static void test_success_and_clash(const char *root) {
    char path[OSPI_PATH * 2], rel[OSPI_PATH], err[300];
    Zip z = {0};
    zip_add(&z, "Read Me", "hello", 5, 0, 0, 0);
    zip_add(&z, "Docs/Notes.txt", "notes", 5, 0, 0, 0);
    vpath(path, sizeof path, root, "Applications/Pack.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Pack.zip", rel, sizeof rel, err, sizeof err) == 0);
    CHECK(strcmp(rel, "Applications/Pack") == 0);
    CHECK(exists(root, "Applications/Pack/Read Me") && exists(root, "Applications/Pack/Docs/Notes.txt"));
    CHECK(exists(root, "Applications/Pack.zip"));                       /* the archive is kept */
    CHECK(!exists(root, "Applications/.Expanding"));                    /* no working folder left behind */
    /* Name clash: the earlier folder is untouched and a new one is made. */
    vpath(path, sizeof path, root, "Applications/Pack/Mine");
    CHECK(write_bytes(path, "keep", 4) == 0);
    CHECK(install_archive(root, "Applications/Pack.zip", rel, sizeof rel, err, sizeof err) == 0);
    CHECK(strcmp(rel, "Applications/Pack 2") == 0 && exists(root, "Applications/Pack 2/Read Me"));
    CHECK(exists(root, "Applications/Pack/Mine") && !exists(root, "Applications/Pack 2/Mine"));
    CHECK(!exists(root, "Applications/.Expanding"));
}

static void test_traversal_and_symlink(const char *root) {
    char path[OSPI_PATH * 2], rel[OSPI_PATH], err[300], outside[OSPI_PATH * 2], abs_name[OSPI_PATH * 2];
    struct stat lst;
    Zip z = {0};
    vpath(outside, sizeof outside, root, "Outside");
    CHECK(mkdir(outside, 0755) == 0);
    zip_add(&z, "ok.txt", "x", 1, 0, 0, 0);
    zip_add(&z, "../../Outside/escape.txt", "x", 1, 0, 0, 0);
    vpath(path, sizeof path, root, "Applications/Dots.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Dots.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(err[0] != 0 && rel[0] == 0);
    CHECK(!exists(root, "Outside/escape.txt") && !exists(root, "Applications/Dots") && !exists(root, "Applications/escape.txt"));
    snprintf(abs_name, sizeof abs_name, "%s/escape-abs.txt", outside);
    zip_add(&z, abs_name, "x", 1, 0, 0, 0);
    vpath(path, sizeof path, root, "Applications/Abs.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Abs.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(!exists(root, "Outside/escape-abs.txt") && !exists(root, "Applications/Abs"));
    /* A symlink entry that points outside, followed by a file written through it. */
    zip_add(&z, "link", outside, strlen(outside), 0120777, 0, 0);
    zip_add(&z, "link/pwn.txt", "x", 1, 0, 0, 0);
    vpath(path, sizeof path, root, "Applications/Link.zip");
    CHECK(zip_write(&z, path) == 0);
    (void)install_archive(root, "Applications/Link.zip", rel, sizeof rel, err, sizeof err);
    CHECK(dir_entries(root, "Outside") == 0);                           /* nothing reached the outside folder */
    snprintf(abs_name, sizeof abs_name, "%s/Applications/Link/link", root);
    CHECK(lstat(abs_name, &lst) != 0 || !S_ISLNK(lst.st_mode));         /* never materialized as a symlink */
    CHECK(!exists(root, "Applications/.Expanding"));
}

static void test_capacity(const char *root) {
    char path[OSPI_PATH * 2], rel[OSPI_PATH], err[300], msg[200];
    size_t big = (size_t)72 << 20;
    uint8_t *blob = calloc(1, big);
    uint64_t old = 0, now = 0, used = 0;
    int stored = 0;
    Zip z = {0};
    CHECK(blob != NULL);
    if (!blob) return;
    zip_add(&z, "Big", blob, big, 0, 0, 0);
    free(blob);
    vpath(path, sizeof path, root, "Applications/Big.zip");
    CHECK(zip_write(&z, path) == 0);
    volume_capacity_load(root, &old, &stored);
    CHECK(volume_used_bytes(root, &used) == 0);
    /* Room for what is on the disk plus 20 MiB: the 72 MiB expansion cannot fit. */
    CHECK(volume_capacity_set(root, used + ((uint64_t)20 << 20), msg, sizeof msg) == 0);
    volume_capacity_load(root, &now, &stored);
    CHECK(now >= used);
    CHECK(install_archive(root, "Applications/Big.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(err[0] != 0 && rel[0] == 0);
    CHECK(!exists(root, "Applications/Big") && !exists(root, "Applications/.Expanding"));
    volume_capacity_set(root, old, msg, sizeof msg);
    unlink(path);
}

static void test_bad_archives(const char *root) {
    char path[OSPI_PATH * 2], rel[OSPI_PATH], err[300];
    uint8_t sit[200];
    Zip z = {0};
    zip_add(&z, "A", "good", 4, 0, 0, 0);
    zip_add(&z, "B", "bad!", 4, 0, 0, 1);
    vpath(path, sizeof path, root, "Applications/Crc.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Crc.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "Could not expand") != NULL && !exists(root, "Applications/Crc"));
    zip_add(&z, "Secret", "xxxx", 4, 0, 1, 0);                          /* flag bit 0: encrypted */
    vpath(path, sizeof path, root, "Applications/Enc.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Enc.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "Could not expand") != NULL && !exists(root, "Applications/Enc"));
    zip_add(&z, "A", "good", 4, 0, 0, 0);                               /* truncated: the central directory is cut off */
    vpath(path, sizeof path, root, "Applications/Cut.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(truncate(path, 40) == 0);
    CHECK(install_archive(root, "Applications/Cut.zip", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(!exists(root, "Applications/Cut"));
    /* StuffIt signature with garbage after it, and no unar to fall back on. */
    memset(sit, 0x41, sizeof sit);
    memcpy(sit, "SIT!", 4); memcpy(sit + 10, "rLau", 4);
    vpath(path, sizeof path, root, "Applications/Bad.sit");
    CHECK(write_bytes(path, sit, sizeof sit) == 0);
    CHECK(setenv("OS999_UNAR", "/nonexistent/unar", 1) == 0);
    CHECK(install_archive(root, "Applications/Bad.sit", rel, sizeof rel, err, sizeof err) != 0);
    CHECK(strstr(err, "Could not expand") != NULL && !exists(root, "Applications/Bad"));
    CHECK(!exists(root, "Applications/.Expanding"));
    /* Content, not the name, identifies it: the same bytes with no extension are still a StuffIt archive. */
    vpath(path, sizeof path, root, "Applications/Mystery");
    CHECK(write_bytes(path, sit, sizeof sit) == 0 && install_archive_sniff(path));
    vpath(path, sizeof path, root, "Applications/Read.txt");
    CHECK(write_bytes(path, "plain text, not an archive", 26) == 0 && !install_archive_sniff(path));
    unsetenv("OS999_UNAR");
}

static void test_nothing_runs(const char *root) {
    char path[OSPI_PATH * 2], rel[OSPI_PATH], err[300], sentinel[OSPI_PATH * 2], script[OSPI_PATH * 2 + 64];
    struct stat st;
    Zip z = {0};
    snprintf(sentinel, sizeof sentinel, "%s/ran-sentinel", root);
    snprintf(script, sizeof script, "#!/bin/sh\ntouch '%s'\n", sentinel);
    zip_add(&z, "run.sh", script, strlen(script), 0100755, 0, 0);
    zip_add(&z, "Setup", script, strlen(script), 0100755, 0, 0);
    vpath(path, sizeof path, root, "Applications/Exec.zip");
    CHECK(zip_write(&z, path) == 0);
    CHECK(install_archive(root, "Applications/Exec.zip", rel, sizeof rel, err, sizeof err) == 0);
    snprintf(path, sizeof path, "%s/%s/run.sh", root, rel);
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0111) == 0);            /* stored as data, no exec bit */
    CHECK(access(sentinel, F_OK) != 0);
}

/* A tiny valid resource fork holding one CODE 0 resource: enough for the Finder to call the file an application. */
static void code_fork(uint8_t *b) {
    memset(b, 0, 326);
    b[2] = 1; b[3] = 0;                               /* data offset 256 */
    b[6] = 1; b[7] = 0x14;                            /* map offset 276 */
    b[11] = 20; b[15] = 50;                           /* data and map lengths */
    b[256 + 3] = 16;                                  /* one 16-byte data block */
    b[276 + 25] = 28; b[276 + 27] = 50;               /* type list and name list offsets */
    memcpy(b + 306, "CODE", 4);
    b[313] = 10;                                      /* ref list offset */
    b[316] = 0xff; b[317] = 0xff;                     /* no name */
}

static void test_dialog_flow(const char *root) {
    StMacFile f;
    StBytes mb = {0};
    StErr e;
    char path[OSPI_PATH * 2];
    int x, y, idx = -1, wi;
    st_macfile_init(&f);
    snprintf(f.name, sizeof f.name, "Solo Game");
    st_mac_set_type_creator(&f, 0x4150504Cu, 0x534F4C4Fu);              /* APPL / SOLO */
    st_bytes_append(&f.data, "data", 4);
    uint8_t fork[326];
    code_fork(fork);
    st_bytes_append(&f.rsrc, fork, sizeof fork);
    CHECK(st_macbinary_encode(&f, &mb, &e) == ST_OK);
    vpath(path, sizeof path, root, "Applications/Solo.bin");
    CHECK(write_bytes(path, mb.p, mb.n) == 0);
    st_bytes_free(&mb);
    st_macfile_free(&f);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return;
    CHECK(open_folder(s, "Applications") == 0);
    Window *w = &s->win[s->nwin - 1];
    for (int i = 0; i < w->nitems; i++) if (!strcmp(w->items[i].name, "Solo.bin")) idx = i;
    CHECK(idx >= 0);
    if (idx < 0) { session_destroy(s); return; }
    open_item(s, w, idx);
    /* Opening only asks: nothing is created until Expand is chosen. */
    CHECK(s->dialog == 2 && s->dialog_cmd == CMD_EXPAND_ARCHIVE_DO);
    CHECK(strstr(s->dialog_body, "Solo.bin") && strstr(s->dialog_body, "\"Solo\"") && strstr(s->dialog_body, "Applications"));
    CHECK(!exists(root, "Applications/Solo"));
    CHECK(ospi_dialog_center(s, 1, &x, &y) == 0);
    session_down(s, x, y, 1); session_up(s, x, y);                      /* Cancel */
    CHECK(s->dialog == 0 && !exists(root, "Applications/Solo"));
    open_item(s, w, idx);
    CHECK(ospi_dialog_center(s, 0, &x, &y) == 0);
    session_down(s, x, y, 1); session_up(s, x, y);                      /* Expand */
    CHECK(exists(root, "Applications/Solo/Solo Game") && exists(root, "Applications/Solo/Solo Game.rsrc"));
    wi = find_win(s, "Solo");
    CHECK(wi >= 0);
    if (wi >= 0) {
        int sel = 0, apps = 0;
        for (int i = 0; i < s->win[wi].nitems; i++) {
            apps += s->win[wi].items[i].kind == ITEM_APPLICATION;
            sel += s->win[wi].items[i].selected;
        }
        CHECK(apps == 1 && sel == 1);                                   /* the one application is selected */
    }
    session_destroy(s);
}

int main(void) {
    char tmpl[OSPI_PATH], top[OSPI_PATH * 2], apps[OSPI_PATH * 2], cmd[OSPI_PATH * 2 + 32];
    snprintf(tmpl, sizeof tmpl, "%s/ospi-expand-test-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    char *root = mkdtemp(tmpl);
    CHECK(root != NULL);
    if (!root) return 1;
    snprintf(top, sizeof top, "%s/Volume", root);
    CHECK(mkdir(top, 0755) == 0);
    snprintf(apps, sizeof apps, "%s/Applications", top);
    CHECK(volume_mkdir_p(apps) == 0);
    test_success_and_clash(top);
    test_traversal_and_symlink(top);
    test_bad_archives(top);
    test_nothing_runs(top);
    test_capacity(top);
    test_dialog_flow(top);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (fails == 0) (void)system(cmd); else fprintf(stderr, "left fixture at %s\n", root);
    if (fails) { fprintf(stderr, "%d check(s) failed\n", fails); return 1; }
    puts("sit expand ok");
    return 0;
}
