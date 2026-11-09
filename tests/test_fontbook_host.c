#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
/* Headless checks for Font Book's opt-in, read-only host font listing.
   Every root is a fake directory under a mkdtemp tree; the real host font
   directories are never opened. */
#include "font_book.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static int copy_file(const char *from, const char *to) {
    FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
    char buf[65536];
    size_t n;
    int ok = in && out;
    while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    if (in) fclose(in);
    if (out && fclose(out) != 0) ok = 0;
    return ok ? 0 : -1;
}

static void remove_tree(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
    chmod(path, 0755);
    DIR *dir = opendir(path);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char child[OSPI_PATH * 2];
            snprintf(child, sizeof child, "%s/%s", path, e->d_name);
            remove_tree(child);
        }
        closedir(dir);
    }
    rmdir(path);
}

/* Counts every entry under path and sums sizes: a cheap "did anything change" fingerprint. */
static void fingerprint(const char *path, long *entries, long long *bytes) {
    struct stat st;
    if (lstat(path, &st) != 0) return;
    (*entries)++;
    if (S_ISREG(st.st_mode)) *bytes += st.st_size;
    if (!S_ISDIR(st.st_mode)) return;
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[OSPI_PATH * 2];
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        fingerprint(child, entries, bytes);
    }
    closedir(dir);
}

static int count_host(const FontBookCatalog *c) {
    int n = 0;
    for (int i = 0; i < c->count; i++) n += c->fonts[i].source == FONT_BOOK_SOURCE_HOST;
    return n;
}

static int count_path(const FontBookCatalog *c, const char *needle) {
    int n = 0;
    for (int i = 0; i < c->count; i++) n += strstr(c->fonts[i].path, needle) != NULL;
    return n;
}

static void path_join(char *out, size_t size, const char *a, const char *b) {
    snprintf(out, size, "%s/%s", a, b);
}

int main(void) {
    char base[] = "/tmp/os999-fbhost-XXXXXX";
    CHECK(mkdtemp(base) != NULL);
    char volume[OSPI_PATH], hostA[OSPI_PATH], hostB[OSPI_PATH], nobundle[OSPI_PATH], p[OSPI_PATH * 2];
    path_join(volume, sizeof volume, base, "vol");
    path_join(hostA, sizeof hostA, base, "hostA");
    path_join(hostB, sizeof hostB, base, "hostB");
    path_join(nobundle, sizeof nobundle, base, "none");
    CHECK(mkdir(volume, 0755) == 0 && mkdir(hostA, 0755) == 0 && mkdir(hostB, 0755) == 0 && mkdir(nobundle, 0755) == 0);

    /* Fixtures: a TrueType, a 10-face collection, and the shapes the scanner must refuse. */
    path_join(p, sizeof p, hostA, "Fake Sans.ttf");  CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);
    path_join(p, sizeof p, hostA, "Fake CJK.ttc");   CHECK(copy_file("fonts/system/NotoSansCJK-Regular.ttc", p) == 0);
    path_join(p, sizeof p, hostA, "Broken.otf");     { FILE *f = fopen(p, "wb"); fputs("not a font at all", f); fclose(f); }
    path_join(p, sizeof p, hostA, "Huge.ttf");       { FILE *f = fopen(p, "wb"); CHECK(f && ftruncate(fileno(f), 40 * 1024 * 1024) == 0); fclose(f); }
    path_join(p, sizeof p, hostA, "Locked.ttf");     CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0 && chmod(p, 0) == 0);
    char target[OSPI_PATH], outside[OSPI_PATH];
    path_join(outside, sizeof outside, base, "outside"); CHECK(mkdir(outside, 0755) == 0);
    path_join(p, sizeof p, outside, "Outside.ttf");  CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);
    snprintf(target, sizeof target, "%s", p);
    path_join(p, sizeof p, hostA, "Linked.ttf");     CHECK(symlink(target, p) == 0);
    path_join(p, sizeof p, hostA, "LinkedDir");      CHECK(symlink(outside, p) == 0);
    char deep[OSPI_PATH * 2];
    snprintf(deep, sizeof deep, "%s/d1/d2/d3", hostA);
    snprintf(p, sizeof p, "%s/d1", hostA); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/d1/d2", hostA); mkdir(p, 0755);
    CHECK(mkdir(deep, 0755) == 0);
    snprintf(p, sizeof p, "%s/Depth3.ttf", deep); CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);
    snprintf(p, sizeof p, "%s/d4", deep); CHECK(mkdir(p, 0755) == 0);
    snprintf(p, sizeof p, "%s/d4/Depth4.ttf", deep); CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);
    snprintf(p, sizeof p, "%s/Closed", hostA); CHECK(mkdir(p, 0755) == 0);
    snprintf(p, sizeof p, "%s/Closed/Hidden.ttf", hostA); CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);
    snprintf(p, sizeof p, "%s/Closed", hostA); CHECK(chmod(p, 0) == 0);
    path_join(p, sizeof p, hostB, "Second.ttf");     CHECK(copy_file("fonts/ChicagoFLF.ttf", p) == 0);

    long fix_entries = 0; long long fix_bytes = 0;
    fingerprint(hostA, &fix_entries, &fix_bytes);
    fingerprint(hostB, &fix_entries, &fix_bytes);

    /* Off by default; fake roots alone list nothing. */
    FontBookCatalog *c = calloc(1, sizeof *c);
    CHECK(c && font_book_catalog_init(c, volume, nobundle, NULL) >= 0);
    const char *roots[2] = {hostA, hostB};
    CHECK(font_book_host_set_roots(c, roots, 2) == 2);
    CHECK(!c->host_enabled);
    CHECK(font_book_catalog_scan(c) >= 0 && count_host(c) == 0);
    char settings[OSPI_PATH * 2];
    snprintf(settings, sizeof settings, "%s/System Folder/Preferences/Font Book 999 Host Fonts", volume);
    CHECK(access(settings, F_OK) != 0);

    /* Default roots are the host's own font folders, and setting them scans nothing. */
    FontBookCatalog *d = calloc(1, sizeof *d);
    CHECK(d && font_book_catalog_init(d, volume, nobundle, NULL) >= 0);
    CHECK(font_book_host_default_roots(d) >= 2);
    CHECK(!strcmp(d->host_roots[0], "/System/Library/Fonts") && !strcmp(d->host_roots[1], "/Library/Fonts"));
    CHECK(count_host(d) == 0 && !d->host_enabled);
    free(d);

    /* Turn on: lists in place, read-only, persisted. */
    char err[FONT_BOOK_ERROR] = {0};
    CHECK(font_book_host_set_enabled(c, 1, err, sizeof err) == 0 && c->host_enabled);
    int cjk = 0, chicago = 0, second = 0;
    for (int i = 0; i < c->count; i++) {
        const FontBookEntry *e = &c->fonts[i];
        if (e->source != FONT_BOOK_SOURCE_HOST) continue;
        CHECK(e->valid && !e->active && !strncmp(e->key, "host:", 5));
        CHECK(!strncmp(e->path, hostA, strlen(hostA)) || !strncmp(e->path, hostB, strlen(hostB)));
        cjk += strstr(e->path, "Fake CJK.ttc") != NULL;
        chicago += strstr(e->family, "Chicago") != NULL;
        second += strstr(e->path, "Second.ttf") != NULL;
        CHECK(font_book_set_active(c, i, 1, err, sizeof err) == -1);
    }
    CHECK(cjk == 10 && second == 1 && chicago >= 3);
    CHECK(count_path(c, "Fake Sans.ttf") == 1 && count_path(c, "Depth3.ttf") == 1);

    /* Bounds: links, depth, size, unreadable, invalid. */
    CHECK(count_path(c, "Linked.ttf") == 0 && count_path(c, "Outside.ttf") == 0);
    CHECK(count_path(c, "Depth4.ttf") == 0);
    CHECK(count_path(c, "Huge.ttf") == 0 && count_path(c, "Broken.otf") == 0);
    CHECK(count_path(c, "Locked.ttf") == 0 && count_path(c, "Hidden.ttf") == 0);

    /* Persisted per volume: a fresh catalog on the same volume starts on, then off. */
    FontBookCatalog *r = calloc(1, sizeof *r);
    CHECK(r && font_book_catalog_init(r, volume, nobundle, NULL) >= 0 && r->host_enabled);
    font_book_host_set_roots(r, roots, 2);
    CHECK(font_book_catalog_scan(r) > 0 && count_host(r) == count_host(c));
    CHECK(font_book_host_set_enabled(r, 0, err, sizeof err) == 0 && !r->host_enabled && count_host(r) == 0);
    CHECK(font_book_catalog_init(r, volume, nobundle, NULL) >= 0 && !r->host_enabled && count_host(r) == 0);
    font_book_host_set_roots(r, roots, 2);
    CHECK(font_book_catalog_scan(r) >= 0 && count_host(r) == 0);
    free(r);

    /* A different volume is unaffected. */
    char volume2[OSPI_PATH];
    path_join(volume2, sizeof volume2, base, "vol2");
    CHECK(mkdir(volume2, 0755) == 0);
    FontBookCatalog *o = calloc(1, sizeof *o);
    CHECK(o && font_book_catalog_init(o, volume2, nobundle, NULL) >= 0 && !o->host_enabled);
    free(o);

    /* Entry cap: far more faces than the limit still stops at it. */
    char many[OSPI_PATH];
    path_join(many, sizeof many, base, "many");
    CHECK(mkdir(many, 0755) == 0);
    for (int i = 0; i < FONT_BOOK_HOST_LIMIT + 40; i++) {
        snprintf(p, sizeof p, "%s/F%04d.ttf", many, i);
        if (copy_file("fonts/ChicagoFLF.ttf", p) != 0) { CHECK(0); break; }
    }
    const char *mroot[1] = {many};
    font_book_host_set_roots(c, mroot, 1);
    CHECK(font_book_catalog_scan(c) > 0 && count_host(c) == FONT_BOOK_HOST_LIMIT);
    CHECK(c->count <= FONT_BOOK_MAX_FONTS);
    CHECK(font_book_host_set_enabled(c, 0, err, sizeof err) == 0);

    /* Nothing written: fake host trees unchanged, no Fonts folder, no host font in the volume. */
    long e2 = 0; long long b2 = 0;
    fingerprint(hostA, &e2, &b2); fingerprint(hostB, &e2, &b2);
    CHECK(e2 == fix_entries && b2 == fix_bytes);
    snprintf(p, sizeof p, "%s/System Folder/Fonts", volume);
    long fe = 0; long long fb = 0;
    fingerprint(p, &fe, &fb);
    CHECK(fe == 0);
    long ve = 0; long long vb = 0;
    fingerprint(volume, &ve, &vb);
    CHECK(vb < 4096);   /* only the tiny settings file lives in the volume */

    free(c);
    remove_tree(base);
    printf("fontbook_host: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
