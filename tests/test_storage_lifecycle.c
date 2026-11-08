#include "storage_fixture.h"
#include "os999_mount.h"
#include "application_probe.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static int entries(const char *path) {
    DIR *d = opendir(path); int n = 0;
    CHECK(d != NULL);
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d); return n;
}
static void applications(void) {
    StArchive a = {0}; StMacFile f;
    st_macfile_init(&f); snprintf(f.name, sizeof f.name, "Installer");
    st_mac_set_type_creator(&f, 0x4150504c, 0x54455354); fixture_fork(&f, "CODE");
    MacApplicationKind kind; StErr e; size_t index;
    CHECK_RC(mac_application_probe(&f, &kind, &e), ST_OK); CHECK(kind == MAC_APP_68K);
    f.finfo[0] = 'T'; CHECK_RC(mac_application_probe(&f, &kind, &e), ST_ENOENT);
    f.finfo[0] = 'A'; f.rsrc.p[0] = 0xff;
    CHECK_RC(mac_application_probe(&f, &kind, &e), ST_EFORMAT); f.rsrc.p[0] = 0;
    memcpy(f.rsrc.p + 306, "cfrg", 4);
    CHECK_RC(mac_application_probe(&f, &kind, &e), ST_EUNSUPPORTED); CHECK(kind == MAC_APP_CFM);
    memcpy(f.rsrc.p + 306, "CODE", 4);
    CHECK_RC(st_archive_add(&a, "Installer", 0, &f, NULL, &e), ST_OK);
    CHECK_RC(mac_application_select(&a, NULL, &index, &kind, &e), ST_OK); CHECK(index == 0);
    CHECK_RC(mac_application_select(&a, "Other", &index, &kind, &e), ST_ENOENT); CHECK(index == SIZE_MAX);
    st_macfile_init(&f); snprintf(f.name, sizeof f.name, "PPC Installer");
    st_mac_set_type_creator(&f, 0x4150504c, 0x54455354);
    CHECK(!st_bytes_append(&f.data, "Joy!peffpwpc", 12));
    CHECK_RC(st_archive_add(&a, f.name, 0, &f, NULL, &e), ST_OK);
    CHECK_RC(mac_application_select(&a, NULL, &index, &kind, &e), ST_EEXIST);
    CHECK_RC(mac_application_select(&a, "PPC Installer", &index, &kind, &e), ST_EUNSUPPORTED);
    CHECK(kind == MAC_APP_PPC && index == 1);
    CHECK_RC(mac_application_select(&a, "Installer", &index, &kind, &e), ST_OK);
    st_archive_free(&a);
}
static void metadata(const char *root) {
    StVolume *v; StErr e; StMacFile f, got; StBytes ad = {0};
    st_make_file(&f, "App", 41, 57, 17);
    CHECK_RC(st_vol_open(root, NULL, &v, &e), ST_OK);
    CHECK_RC(st_vol_write(v, "App", &f, &e), ST_OK);
    CHECK_RC(st_vol_read(v, "App", &got, NULL, &e), ST_OK);
    CHECK(st_same_file(&f, &got)); st_macfile_free(&got);
    char side[4096]; snprintf(side, sizeof side, "%s/App.rsrc", root);
    CHECK_RC(st_applesingle_encode(&f, 1, &ad, &e), ST_OK);
    ad.p[4] = 0xff; fixture_write(side, &ad);
    CHECK_RC(st_vol_read(v, "App", &got, NULL, &e), ST_EFORMAT);
    StArchive a = {0}; CHECK_RC(st_import_tree(root, &a, NULL, &e), ST_EFORMAT);
    StBytes mb = {0}; StUnpackInfo bad_info;
    CHECK_RC(st_macbinary_encode(&f, &mb, &e), ST_OK); mb.p[65] ^= 1;
    CHECK_RC(st_unpack(mb.p, mb.n, NULL, 0, "broken.bin", &a, &bad_info, NULL, &e), ST_ECHECKSUM);
    st_bytes_free(&mb);
    ad.p[4] = 0; st_put32(ad.p + 30, 0); /* entry points inside header */
    CHECK_RC(st_applesingle_decode(ad.p, ad.n, &got, NULL, &e), ST_EFORMAT);
    CHECK_RC(st_applesingle_encode(&f, 1, &ad, &e), ST_OK);
    memcpy(ad.p + 38, ad.p + 26, 12); /* duplicate descriptor */
    CHECK_RC(st_applesingle_decode(ad.p, ad.n, &got, NULL, &e), ST_EFORMAT);
    CHECK(unlink(side) == 0); CHECK(symlink("App", side) == 0);
    CHECK_RC(st_vol_read(v, "App", &got, NULL, &e), ST_EPATH);
    CHECK(st_import_tree(root, &a, NULL, &e) != ST_OK);
    CHECK(unlink(side) == 0); CHECK(mkfifo(side, 0600) == 0);
    CHECK_RC(st_vol_read(v, "App", &got, NULL, &e), ST_EPATH);
    CHECK(st_import_tree(root, &a, NULL, &e) != ST_OK);
    CHECK(unlink(side) == 0);
    CHECK_RC(st_vol_write(v, "App", &f, &e), ST_OK);
    char path[4096]; snprintf(path, sizeof path, "%s/App", root);
    StUnpackInfo info;
    CHECK_RC(st_unpack_file(path, 0, &a, &info, NULL, &e), ST_OK);
    CHECK(a.n == 1 && st_same_file(&a.v[0].file, &f)); st_archive_free(&a);
    CHECK_RC(st_vol_remove(v, "App", &e), ST_OK);
    StArchive large = {0};
    int built = 1;
    for (int i = 0; i < 4097; i++) {
        StMacFile empty; st_macfile_init(&empty);
        snprintf(empty.name, sizeof empty.name, "file-%d", i);
        if (st_archive_add(&large, empty.name, 0, &empty, NULL, &e)) { built = 0; break; }
    }
    CHECK(built);
    CHECK_RC(st_vol_install(v, "TooMany", &large, &e), ST_ELIMIT);
    snprintf(path, sizeof path, "%s/TooMany", root); CHECK(access(path, F_OK) != 0);
    st_archive_free(&large);
    st_vol_close(v); st_macfile_free(&f); st_bytes_free(&ad);
}
static void images(const char *root, const char *scratch) {
    StArchive a = {0}; StMacFile f; StErr e;
    st_make_file(&f, "Game", 127, 330, 9);
    CHECK_RC(st_archive_add(&a, "Folder/Game", 0, &f, NULL, &e), ST_OK);
    StBytes raw = {0}, dc = {0}, dmg = {0};
    CHECK_RC(st_hfs_write(&a, "Game Disc", 819200, &raw, &e), ST_OK);
    CHECK_RC(st_dc42_encode(raw.p, raw.n, "Game Disc", &dc, &e), ST_OK);
    fixture_udif(&raw, &dmg);
    StBytes *images[] = {&raw, &dc, &dmg};
    const char *names[] = {"hybrid.iso", "floppy.img", "disk.dmg"};
    char file[4096], snapshot[4096];
    for (int i = 0; i < 3; i++) {
        snprintf(file, sizeof file, "%s/%s", root, names[i]); fixture_write(file, images[i]);
        uint32_t crc = st_crc32(0, images[i]->p, images[i]->n);
        StMount *m = NULL;
        CHECK_RC(st_mount_open(file, scratch, NULL, NULL, &m, &e), ST_OK);
        if (!m) continue;
        snprintf(snapshot, sizeof snapshot, "%s", st_mount_path(m));
        CHECK(!strcmp(st_mount_info(m)->volume, "Game Disc"));
        StMacFile got;
        CHECK_RC(st_vol_read(st_mount_volume(m), "Folder/Game", &got, NULL, &e), ST_OK);
        CHECK(st_same_file(&got, &a.v[0].file)); st_macfile_free(&got);
        CHECK_RC(st_vol_write(st_mount_volume(m), "changed", &a.v[0].file, &e), ST_EINVAL);
        CHECK_RC(st_mount_close(m, &e), ST_OK); CHECK(access(snapshot, F_OK) != 0);
        CHECK(entries(scratch) == 0);
        StBytes unchanged = {0}; CHECK_RC(st_read_file(file, 2 << 20, &unchanged, &e), ST_OK);
        CHECK(st_crc32(0, unchanged.p, unchanged.n) == crc); st_bytes_free(&unchanged);
        StVolumeOptions quota = {1, 0, 0};
        CHECK_RC(st_mount_open(file, scratch, &quota, NULL, &m, &e), ST_EQUOTA);
        CHECK(m == NULL && entries(scratch) == 0);
        CHECK(unlink(file) == 0);
    }
    snprintf(file, sizeof file, "%s/editable.img", root); fixture_write(file, &dc);
    StMount *m = NULL; StVolumeOptions rw = {0};
    CHECK_RC(st_mount_open(file, scratch, &rw, NULL, &m, &e), ST_OK);
    if (m) {
        CHECK_RC(st_vol_write(st_mount_volume(m), "new", &a.v[0].file, &e), ST_OK);
        char link[4096]; snprintf(link, sizeof link, "%s/outside", st_mount_path(m));
        CHECK(symlink(root, link) == 0);
        CHECK_RC(st_vol_write(st_mount_volume(m), "outside/escape", &a.v[0].file, &e), ST_EPATH);
        CHECK_RC(st_mount_close(m, &e), ST_OK);
        CHECK(access(file, F_OK) == 0); CHECK(entries(scratch) == 0);
    }
    dc.p[90] ^= 1; fixture_write(file, &dc);
    CHECK_RC(st_mount_open(file, scratch, NULL, NULL, &m, &e), ST_ECHECKSUM); CHECK(!m);
    CHECK(entries(scratch) == 0); CHECK(unlink(file) == 0);
    st_bytes_free(&raw); st_bytes_free(&dc); st_bytes_free(&dmg); st_archive_free(&a);
}
static void iso_disc(const char *root, const char *scratch) {
    StBytes iso = {0}; fixture_iso(&iso);
    char path[4096]; snprintf(path, sizeof path, "%s/data.iso", root); fixture_write(path, &iso);
    StMount *m = NULL; StErr e;
    CHECK_RC(st_mount_open(path, scratch, NULL, NULL, &m, &e), ST_OK);
    if (m) {
        StMacFile f; CHECK_RC(st_vol_read(st_mount_volume(m), "HELLO.TXT", &f, NULL, &e), ST_OK);
        CHECK(f.data.n == 5 && !memcmp(f.data.p, "hello", 5) && f.rsrc.n == 0);
        st_macfile_free(&f); CHECK_RC(st_mount_close(m, &e), ST_OK);
    }
    StArchive a = {0}; char name[256]; uint8_t *r = iso.p + 20 * 2048 + 68;
    r[25] = 0x80;
    CHECK_RC(st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e), ST_EUNSUPPORTED);
    r[25] = 2; both32(r + 2, 20); both32(r + 10, 2048);
    CHECK_RC(st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e), ST_EFORMAT);
    r[25] = 0; both32(r + 2, 99);
    CHECK_RC(st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e), ST_EFORMAT);
    both32(r + 2, 21); both32(r + 10, 5); r[6] ^= 1;
    CHECK_RC(st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e), ST_EFORMAT); r[6] ^= 1;
    memcpy(r + 33, "../evil;001", 11);
    CHECK_RC(st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e), ST_EPATH);
    /* Deterministic malformed ISO inputs exercised under ASan/UBSan too. */
    st_bytes_free(&iso); fixture_iso(&iso);
    uint32_t seed = 91;
    for (int i = 0; i < 1000; i++) {
        size_t off = 16 * 2048 + st_rand(&seed) % (iso.n - 16 * 2048);
        uint8_t old = iso.p[off]; iso.p[off] ^= (uint8_t)(1 + st_rand(&seed) % 255);
        int rc = st_iso_read(iso.p, iso.n, &a, name, sizeof name, NULL, &e);
        CHECK(rc <= ST_OK); st_archive_free(&a); iso.p[off] = old;
    }
    CHECK(unlink(path) == 0); st_bytes_free(&iso);
}
int main(void) {
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "build";
    char root[4096], scratch[4096];
    snprintf(root, sizeof root, "%s/storage-lifecycle-XXXXXX", tmp);
    CHECK(mkdtemp(root) != NULL);
    snprintf(scratch, sizeof scratch, "%s/mounts", root); CHECK(mkdir(scratch, 0700) == 0);
    char fifo[4096]; snprintf(fifo, sizeof fifo, "%s/pipe.img", root);
    CHECK(mkfifo(fifo, 0600) == 0);
    StMount *bad = NULL; StErr err;
    CHECK_RC(st_mount_open(fifo, scratch, NULL, NULL, &bad, &err), ST_EIO);
    CHECK(!bad && entries(scratch) == 0); CHECK(unlink(fifo) == 0);
    applications(); metadata(root); images(root, scratch); iso_disc(root, scratch);
    CHECK(rmdir(scratch) == 0);
    char lock[4096]; snprintf(lock, sizeof lock, "%s/.os999-lock", root); unlink(lock);
    if (rmdir(root)) fprintf(stderr, "fixture retained: %s\n", root);
    return st_test_done("test_storage_lifecycle");
}
