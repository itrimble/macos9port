/* Headless checks for Install Classic Apps: catalogue parsing, hashes, a file:// install through the
 * same pipeline as a download, hash/size mismatch, capacity refusal, cancel, already installed, local
 * sources, failure clean-up, and that nothing installed is ever run. Every archive is built here;
 * no Apple software is used, downloaded or stored by this test. */
#include "ospi.h"
#include "classic_apps.h"
#include "modern_network.h"
#include "os999_storage.h"
#include "volume_capacity.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static int read_all(const char *root, const char *rel, char *out, size_t n) {
    char p[OSPI_PATH * 2];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    size_t got = fread(out, 1, n - 1, f);
    out[got] = 0;
    fclose(f);
    return 0;
}

/* ---- the fake "download": a file:// URL fetched through the (stub) network layer ---- */
typedef struct { char url[OSPI_PATH + 16]; int calls; atomic_bool *cancel_during; int fail; } Fake;
static int fake_fetch(const ClassicApp *app, const char *dest, const atomic_bool *cancel, void *ctx, char *err, size_t en) {
    Fake *f = ctx;
    (void)app; (void)cancel;
    f->calls++;
    if (f->cancel_during) atomic_store(f->cancel_during, 1);
    NetPolicy p = {.max_bytes = 200000000, .timeout_ms = 5000};
    if (f->fail || net_download(f->url, dest, &p).status != NET_OK) { snprintf(err, en, "network unreachable"); return -1; }
    return 0;
}

static void make_pkg(const char *path, size_t pad, int bad_crc) {
    Zip z = {0};
    char *blob = pad ? calloc(1, pad) : NULL;
    zip_add(&z, "Fake Player", "FAKE-PLAYER-CODE", 16, 0755, 0, bad_crc);
    zip_add(&z, "Fake Player.rsrc", "rsrc", 4, 0, 0, 0);
    zip_add(&z, "Run Me", "#!/bin/sh\ntouch \"$MARKER\"\n", 26, 0755, 0, 0);
    if (pad) zip_add(&z, "Pad", blob, pad, 0, 0, 0);
    CHECK(zip_write(&z, path) == 0);
    free(blob);
}

static const char *CAT =
    "# fake catalogue\n[fake-player]\nname = Fake Player\npage = https://macintoshgarden.org/apps/fake\nfile = Fake.zip\n"
    "md5 = 0123456789ABCDEF0123456789abcdef\nsize = 1234\nsize_mb = 0.01\ndest = Applications\nwrap = yes\ndefault = yes\n"
    "note = A test item\n\n[fake-ext]\nname = Fake Extension\ndest = System Folder:Extensions\nwrap = no\n";

static void test_catalogue(void) {
    ClassicCatalog c;
    char err[200];
    CHECK(classic_catalog_parse(CAT, &c, err, sizeof err) == 2);
    const ClassicApp *a = classic_catalog_find(&c, "fake-player");
    CHECK(a && !strcmp(a->name, "Fake Player") && a->size == 1234 && a->wrap && a->default_on);
    CHECK(a && !strcmp(a->md5, "0123456789abcdef0123456789abcdef"));   /* lowercased */
    const ClassicApp *e = classic_catalog_find(&c, "fake-ext");
    CHECK(e && !e->wrap && !e->page[0] && !strcmp(e->dest, "System Folder:Extensions"));
    CHECK(!classic_catalog_find(&c, "nope"));
    static const char *bad[] = {
        "[Bad Id]\nname=x\ndest=Applications\n", "[a]\nname=x\ndest=..:Evil\n", "[a]\nname=x\ndest=A:/B\n", "[a]\nname=x\n",
        "[a]\nname=x\ndest=Applications\nmd5=zz\n", "[a]\nname=x\ndest=Applications\npage=https://example.com/x\n",
        "[a]\nname=x\ndest=Applications\n[a]\nname=y\ndest=Applications\n", "[a]\nname=x\ndest=Applications\nbogus=1\n",
        "name=x\n", "[a]\nname=x\ndest=Applications\nfile=../x\n", "[a]\nname=x\ndest=Applications\nsize=12x\n",
        "[a]\nname=x\ndest=Applications\nwrap=maybe\n"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        CHECK(classic_catalog_parse(bad[i], &c, err, sizeof err) == -1);
        CHECK(err[0] != 0);
    }
    /* The built-in catalogue is metadata only: names, Garden pages, hashes, destinations. */
    CHECK(classic_catalog_default(&c) >= 3);
    int quicktime = 0;
    for (int i = 0; i < c.n; i++) {
        CHECK(!c.app[i].page[0] || garden_page_ok(c.app[i].page));
        CHECK(c.app[i].dest[0]);
        if (strstr(c.app[i].id, "quicktime")) quicktime++;
    }
    CHECK(quicktime >= 1);
}

static void test_hashes(const char *dir) {
    char md5[33], sha[65], path[OSPI_PATH * 2];
    uint64_t size = 0;
    snprintf(path, sizeof path, "%s/h.bin", dir);
    CHECK(write_bytes(path, "abc", 3) == 0 && classic_hash_file(path, md5, sha, &size) == 0 && size == 3);
    CHECK(!strcmp(md5, "900150983cd24fb0d6963f7d28e17f72"));
    CHECK(!strcmp(sha, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK(write_bytes(path, "", 0) == 0 && classic_hash_file(path, md5, sha, &size) == 0 && size == 0);
    CHECK(!strcmp(md5, "d41d8cd98f00b204e9800998ecf8427e"));
    CHECK(!strcmp(sha, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK(classic_hash_file("/nonexistent/file", md5, sha, &size) == -1);
    /* Block-boundary lengths against the host tools. */
    static const size_t lens[] = {55, 56, 63, 64, 65, 119, 120, 128, 1000};
    for (size_t i = 0; i < sizeof lens / sizeof *lens; i++) {
        char buf[1000], cmd[OSPI_PATH * 2 + 40], want[80];
        for (size_t k = 0; k < lens[i]; k++) buf[k] = (char)('a' + k % 26);
        CHECK(write_bytes(path, buf, lens[i]) == 0 && classic_hash_file(path, md5, sha, &size) == 0);
        snprintf(cmd, sizeof cmd, "shasum -a 256 '%s' 2>/dev/null", path);
        FILE *p = popen(cmd, "r");
        if (p && fscanf(p, "%64s", want) == 1) CHECK(!strcmp(want, sha));
        if (p) pclose(p);
        snprintf(cmd, sizeof cmd, "md5 -q '%s' 2>/dev/null", path);
        p = popen(cmd, "r");
        if (p && fscanf(p, "%32s", want) == 1) CHECK(!strcmp(want, md5));
        if (p) pclose(p);
    }
}

static void fresh_catalog_app(ClassicApp *a, const char *md5, uint64_t size) {
    ClassicCatalog c;
    CHECK(classic_catalog_parse(CAT, &c, NULL, 0) == 2);
    *a = *classic_catalog_find(&c, "fake-player");
    a->size = size;
    snprintf(a->md5, sizeof a->md5, "%s", md5);
}

static void test_install_flow(const char *root, const char *scratch) {
    char pkg[OSPI_PATH * 2], rel[OSPI_PATH], err[300], md5[33], sha[65], receipt[1024], marker[OSPI_PATH * 2];
    uint64_t size;
    snprintf(pkg, sizeof pkg, "%s/Fake.zip", scratch);
    snprintf(marker, sizeof marker, "%s/RAN", scratch);
    setenv("MARKER", marker, 1);
    make_pkg(pkg, 0, 0);
    CHECK(classic_hash_file(pkg, md5, sha, &size) == 0);
    Fake fake = {0};
    snprintf(fake.url, sizeof fake.url, "file://%s", pkg);
    ClassicHooks hooks = {.fetch = fake_fetch, .ctx = &fake};
    ClassicApp app;

    /* hash mismatch: refused, nothing on the volume, no receipt */
    fresh_catalog_app(&app, "00000000000000000000000000000000", 0);
    int before = dir_entries(root, "Applications");
    CHECK(classic_install(root, &app, &hooks, rel, sizeof rel, err, sizeof err) == CA_HASH);
    CHECK(dir_entries(root, "Applications") == before && !exists(root, ".os999/installs/fake-player.receipt"));
    CHECK(strstr(err, "checksum") != NULL);
    /* size mismatch */
    fresh_catalog_app(&app, md5, size + 1);
    CHECK(classic_install(root, &app, &hooks, rel, sizeof rel, err, sizeof err) == CA_HASH);
    CHECK(dir_entries(root, "Applications") == before);
    /* download failure */
    fresh_catalog_app(&app, md5, size);
    Fake broken = fake;
    broken.fail = 1;
    ClassicHooks bad_hooks = {.fetch = fake_fetch, .ctx = &broken};
    CHECK(classic_install(root, &app, &bad_hooks, rel, sizeof rel, err, sizeof err) == CA_FAILED && strstr(err, "network"));
    CHECK(dir_entries(root, "Applications") == before);
    /* cancel before starting, and in the middle of the download */
    atomic_bool stop = 0;
    ClassicHooks pre = {.fetch = fake_fetch, .ctx = &fake, .cancel = &stop};
    atomic_store(&stop, 1);
    fake.calls = 0;
    CHECK(classic_install(root, &app, &pre, rel, sizeof rel, err, sizeof err) == CA_CANCELLED && fake.calls == 0);
    atomic_store(&stop, 0);
    Fake mid = fake;
    mid.cancel_during = &stop;
    mid.calls = 0;
    ClassicHooks during = {.fetch = fake_fetch, .ctx = &mid, .cancel = &stop};
    CHECK(classic_install(root, &app, &during, rel, sizeof rel, err, sizeof err) == CA_CANCELLED && mid.calls == 1);
    CHECK(dir_entries(root, "Applications") == before && !exists(root, ".os999/installs/fake-player.receipt"));
    /* good install */
    fake.calls = 0;
    CHECK(classic_install(root, &app, &hooks, rel, sizeof rel, err, sizeof err) == CA_OK);
    CHECK(!strcmp(rel, "Applications/Fake Player"));
    CHECK(exists(root, "Applications/Fake Player/Fake Player") && exists(root, "Applications/Fake Player/Fake Player.rsrc_"));   /* a literal .rsrc name is escaped by the storage stack */
    CHECK(exists(root, "Applications/Fake Player/Run Me"));
    CHECK(access(marker, F_OK) != 0);   /* nothing installed was run */
    char path[OSPI_PATH * 2];
    struct stat st;
    snprintf(path, sizeof path, "%s/Applications/Fake Player/Run Me", root);
    CHECK(stat(path, &st) == 0 && !(st.st_mode & 0111));   /* no execute bit survives */
    CHECK(read_all(root, ".os999/installs/fake-player.receipt", receipt, sizeof receipt) == 0);
    CHECK(strstr(receipt, "verified=yes") && strstr(receipt, "executed=no") && strstr(receipt, md5) && strstr(receipt, sha));
    CHECK(strstr(receipt, "source=https://macintoshgarden.org/apps/fake") && strstr(receipt, "dest_rel=Applications/Fake Player"));
    CHECK(classic_installed(root, &app));
    /* already installed: no download, no new folder */
    int calls = fake.calls;
    CHECK(classic_install(root, &app, &hooks, rel, sizeof rel, err, sizeof err) == CA_ALREADY && fake.calls == calls);
    CHECK(!exists(root, "Applications/Fake Player 2"));
    /* the receipt without its folder no longer counts as installed */
    char cmd[OSPI_PATH * 2 + 16];
    snprintf(path, sizeof path, "%s/Applications/Fake Player", root);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", path);
    CHECK(system(cmd) == 0 && !classic_installed(root, &app));
    /* no wrap into System Folder:Extensions, and no source at all */
    ClassicCatalog c;
    CHECK(classic_catalog_parse(CAT, &c, NULL, 0) == 2);
    ClassicApp ext = *classic_catalog_find(&c, "fake-ext");
    CHECK(classic_install(root, &ext, NULL, rel, sizeof rel, err, sizeof err) == CA_NO_SOURCE);
    CHECK(classic_install(root, &ext, &hooks, rel, sizeof rel, err, sizeof err) == CA_OK);
    CHECK(!strcmp(rel, "System Folder/Extensions") && exists(root, "System Folder/Extensions/Fake Player"));
    CHECK(classic_install(root, &ext, &hooks, rel, sizeof rel, err, sizeof err) == CA_ALREADY);
    CHECK(access(marker, F_OK) != 0);
    /* a corrupt archive leaves no folder behind */
    make_pkg(pkg, 0, 1);
    ClassicApp bad;
    fresh_catalog_app(&bad, "", 0);
    bad.size = 0;
    snprintf(bad.id, sizeof bad.id, "corrupt");
    snprintf(bad.name, sizeof bad.name, "Corrupt");
    before = dir_entries(root, "Applications");
    CHECK(classic_install(root, &bad, &hooks, rel, sizeof rel, err, sizeof err) == CA_FAILED);
    CHECK(dir_entries(root, "Applications") == before && !exists(root, ".os999/installs/corrupt.receipt"));
}

static void test_capacity(const char *root, const char *scratch) {
    char pkg[OSPI_PATH * 2], rel[OSPI_PATH], err[300], msg[200], filler[OSPI_PATH * 2];
    snprintf(pkg, sizeof pkg, "%s/Big.zip", scratch);
    make_pkg(pkg, 3u << 20, 0);
    Fake fake = {0};
    snprintf(fake.url, sizeof fake.url, "file://%s", pkg);
    ClassicHooks hooks = {.fetch = fake_fetch, .ctx = &fake};
    ClassicApp app;
    fresh_catalog_app(&app, "", 0);
    app.size = 0;
    snprintf(app.id, sizeof app.id, "big");
    snprintf(app.name, sizeof app.name, "Big");
    uint64_t old = 0, used = 0;
    CHECK(volume_capacity_load(root, &old, NULL) == 0);
    CHECK(volume_capacity_set(root, VOLUME_CAPACITY_MIN_BYTES, msg, sizeof msg) == 0);
    volume_used_bytes(root, &used);
    uint64_t room = VOLUME_CAPACITY_MIN_BYTES > used ? VOLUME_CAPACITY_MIN_BYTES - used : 0;
    size_t fill = room > (1u << 20) ? (size_t)(room - (1u << 20)) : 0;   /* leave ~1 MB: less than the 3 MB archive */
    char *blob = calloc(1, fill ? fill : 1);
    snprintf(filler, sizeof filler, "%s/Documents/filler.bin", root);
    CHECK(write_bytes(filler, blob, fill) == 0);
    free(blob);
    volume_space_invalidate();
    int before = dir_entries(root, "Applications");
    CHECK(classic_install(root, &app, &hooks, rel, sizeof rel, err, sizeof err) == CA_FULL);
    CHECK(err[0] != 0 && dir_entries(root, "Applications") == before && !exists(root, ".os999/installs/big.receipt"));
    unlink(filler);
    volume_capacity_set(root, old, msg, sizeof msg);
    volume_space_invalidate();
}

static void test_local(const char *root, const char *scratch) {
    char pkg[OSPI_PATH * 2], rel[OSPI_PATH], err[300], dir[OSPI_PATH * 2], p[OSPI_PATH * 2], receipt[1024];
    snprintf(pkg, sizeof pkg, "%s/My Local App.zip", scratch);
    make_pkg(pkg, 0, 0);
    CHECK(classic_install_local(root, pkg, NULL, rel, sizeof rel, err, sizeof err) == CA_OK);
    CHECK(!strcmp(rel, "Applications/My Local App") && exists(root, "Applications/My Local App/Fake Player"));
    CHECK(read_all(root, ".os999/installs/local-my-local-app.receipt", receipt, sizeof receipt) == 0 && strstr(receipt, "source=local"));
    CHECK(!strstr(receipt, scratch));   /* the receipt does not record the user's host path */
    CHECK(classic_install_local(root, pkg, NULL, rel, sizeof rel, err, sizeof err) == CA_ALREADY);
    /* a folder: files and sidecars copied, symlinks skipped, no execute bit */
    snprintf(dir, sizeof dir, "%s/Some Folder", scratch);
    snprintf(p, sizeof p, "%s/Sub", dir);
    CHECK(mkdir(dir, 0755) == 0 && mkdir(p, 0755) == 0);
    snprintf(p, sizeof p, "%s/Tool", dir);
    CHECK(write_bytes(p, "code", 4) == 0 && chmod(p, 0755) == 0);
    snprintf(p, sizeof p, "%s/Tool.rsrc", dir);
    CHECK(write_bytes(p, "fork", 4) == 0);
    snprintf(p, sizeof p, "%s/Sub/Doc", dir);
    CHECK(write_bytes(p, "doc", 3) == 0);
    snprintf(p, sizeof p, "%s/Link", dir);
    CHECK(symlink("/etc/hosts", p) == 0);
    CHECK(classic_install_local(root, dir, NULL, rel, sizeof rel, err, sizeof err) == CA_OK);
    CHECK(!strcmp(rel, "Applications/Some Folder") && exists(root, "Applications/Some Folder/Tool.rsrc") &&
          exists(root, "Applications/Some Folder/Sub/Doc") && !exists(root, "Applications/Some Folder/Link"));
    snprintf(p, sizeof p, "%s/Applications/Some Folder/Tool", root);
    struct stat st;
    CHECK(stat(p, &st) == 0 && !(st.st_mode & 0111));
    /* bad sources */
    CHECK(classic_install_local(root, "/nonexistent/thing", NULL, rel, sizeof rel, err, sizeof err) == CA_NO_SOURCE);
    CHECK(classic_install_local(root, "relative/path", NULL, rel, sizeof rel, err, sizeof err) == CA_NO_SOURCE);
    /* cancel before a local install starts */
    atomic_bool stop = 1;
    ClassicHooks hooks = {.cancel = &stop};
    snprintf(p, sizeof p, "%s/Other.zip", scratch);
    make_pkg(p, 0, 0);
    CHECK(classic_install_local(root, p, &hooks, rel, sizeof rel, err, sizeof err) == CA_CANCELLED);
    CHECK(!exists(root, "Applications/Other"));
}

static void test_pick_and_panel(const char *root) {
    char url[300];
    const char *html = "<a href=\"//download.macintoshgarden.org/apps/Other_QT.sit?expires=1&amp;token=a\">x</a>"
                       "<a href=\"//old.mac.gdn/apps/QT.sit\">m</a>"
                       "<a href=\"//download.macintoshgarden.org/apps/QT.sit?expires=1&amp;token=b\">y</a>";
    CHECK(classic_pick_named(html, "QT.sit", url, sizeof url) == 0);
    CHECK(!strcmp(url, "https://download.macintoshgarden.org/apps/QT.sit?expires=1&token=b"));
    CHECK(classic_pick_named(html, "QT", url, sizeof url) == -1);              /* whole file name only */
    CHECK(classic_pick_named(html, "Missing.sit", url, sizeof url) == -1);
    CHECK(classic_pick_named("<a href=\"//evil.example/apps/QT.sit\">", "QT.sit", url, sizeof url) == -1);
    /* The dialog opens once, paints, and the first-run offer is made only once per volume. */
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return;
    CHECK(classic_apps_first_run(s) == 1 && classic_apps_first_run(s) == 0);
    int idx = -1;
    for (int i = 0; i < s->nwin; i++) if (!strcmp(s->win[i].rel, ":classicapps")) idx = i;
    CHECK(idx >= 0);
    uint32_t *px = calloc((size_t)OSPI_W * OSPI_H, 4);
    session_paint(s, px, OSPI_W, OSPI_H);
    int inked = 0;
    for (int i = 0; i < OSPI_W * OSPI_H; i++) inked += px[i] != px[0];
    CHECK(inked > 1000);
    Layout lay;
    if (idx >= 0) window_layout(&s->win[idx], &lay);
    /* "From Folder or Archive..." opens the in-OS999 file dialog, which paints too. */
    if (idx >= 0) {
        session_down(s, lay.content.x + 200, lay.content.y + lay.content.h - 22, 1);
        session_up(s, lay.content.x + 200, lay.content.y + lay.content.h - 22);
        session_paint(s, px, OSPI_W, OSPI_H);
    }
    free(px);
    session_destroy(s);
}

int main(void) {
    char tmpl[] = "/tmp/os999-classicXXXXXX", scratch[OSPI_PATH * 2];
    char *base = mkdtemp(tmpl);
    CHECK(base != NULL);
    if (!base) return 1;
    char root[OSPI_PATH];
    snprintf(root, sizeof root, "%s/vol", base);
    snprintf(scratch, sizeof scratch, "%s/scratch", base);
    CHECK(mkdir(scratch, 0755) == 0);
    CHECK(volume_seed(root) == 0);
    os999_private_set_root(root);
    test_catalogue();
    test_hashes(scratch);
    test_install_flow(root, scratch);
    test_capacity(root, scratch);
    test_local(root, scratch);
    test_pick_and_panel(root);
    char cmd[OSPI_PATH * 2];
    if (getenv("KEEP_CLASSIC_TEST_DIR")) { printf("kept %s\n", base); return fails != 0; }
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", base);
    if (system(cmd) != 0) fprintf(stderr, "could not remove %s\n", base);
    if (fails) { fprintf(stderr, "%d classic apps checks failed\n", fails); return 1; }
    printf("classic apps checks passed\n");
    return 0;
}
