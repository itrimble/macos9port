/* Compatibility with the OS999 runtime: a file saved through a guest
 * volume must be readable by the desktop's own resource-fork loader
 * (src/mac_resource.c) from its "<name>.rsrc" AppleDouble sidecar.
 * Also exercises st_find_resource and an install manifest left behind
 * by a crash. */
#include "st_test.h"
#include "mac_resource.h"

#include <sys/stat.h>
#include <unistd.h>

int st_find_resource(const uint8_t *fork, size_t n, uint32_t type, int16_t id, const uint8_t **data, size_t *size);

static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { be16(p, (uint16_t)(v >> 16)); be16(p + 2, (uint16_t)v); }

/* A resource fork with one resource: CODE 0 or any type/id. */
static void make_fork(StBytes *f, uint32_t type, int16_t id, const void *payload, size_t pl) {
    size_t data_len = 4 + pl, map_len = 28 + 2 + 8 + 12;
    f->n = 0;
    st_bytes_reserve(f, 256 + data_len + map_len);
    memset(f->p, 0, 256 + data_len + map_len);
    f->n = 256 + data_len + map_len;
    uint8_t *h = f->p;
    be32(h, 256);
    be32(h + 4, (uint32_t)(256 + data_len));
    be32(h + 8, (uint32_t)data_len);
    be32(h + 12, (uint32_t)map_len);
    be32(h + 256, (uint32_t)pl);
    memcpy(h + 260, payload, pl);
    uint8_t *m = h + 256 + data_len;
    memcpy(m, h, 16);
    be16(m + 24, 28);
    be16(m + 26, (uint16_t)map_len);
    be16(m + 28, 0);          /* one type */
    be32(m + 30, type);
    be16(m + 34, 0);          /* one resource */
    be16(m + 36, 10);         /* refs at type list + 10 */
    uint8_t *r = m + 28 + 10;
    be16(r, (uint16_t)id);
    be16(r + 2, 0xFFFF);
    be32(r + 4, 0);           /* attrs 0, data offset 0 */
}

static uint32_t dc6_crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1) ? (crc >> 1) ^ 0x04C11DB7u : crc >> 1;
    }
    return crc;
}

/* NDIF: an HFS floppy split into raw, zero and ADC chunks behind a
 * 16-byte backing offset, described by a bcem resource. */
static void test_ndif(void) {
    StArchive a = {0}, b = {0};
    StMacFile f;
    st_make_file(&f, "Inside NDIF", 30000, 500, 81);
    st_archive_add(&a, "Folder/Inside NDIF", 0, &f, NULL, NULL);
    StBytes img = {0}, data = {0}, bcem = {0}, rsrc = {0};
    StErr err;
    CHECK_RC(st_hfs_write(&a, "NDIF Disk", 819200, &img, &err), ST_OK);
    uint32_t zs = 0;
    for (uint32_t s = 200; s + 8 < 1500 && !zs; s++) {
        int zero = 1;
        for (size_t k = (size_t)s * 512; k < (size_t)(s + 8) * 512 && zero; k++) zero = !img.p[k];
        if (zero) zs = s;
    }
    CHECK(zs > 0);
    uint8_t pad[16] = {0};
    st_bytes_append(&data, pad, 16);
    uint8_t hdr[128] = {0};
    be16(hdr, 10);
    hdr[4] = 9;
    memcpy(hdr + 5, "NDIF Disk", 9);
    be32(hdr + 68, 1600);
    be32(hdr + 76, 16);
    be32(hdr + 80, dc6_crc(img.p, img.n));
    be32(hdr + 124, 4);
    st_bytes_append(&bcem, hdr, 128);
    uint32_t starts[4] = {0, zs, zs + 8, 1600};
    uint8_t types[4] = {2, 0, 0x83, 0xFF};
    for (int c = 0; c < 4; c++) {
        uint8_t e[12] = {(uint8_t)(starts[c] >> 16), (uint8_t)(starts[c] >> 8), (uint8_t)starts[c], types[c]};
        size_t at = data.n - 16;
        if (types[c] == 2) st_bytes_append(&data, img.p, (size_t)zs * 512);
        if (types[c] == 0x83)
            for (size_t k = (size_t)(zs + 8) * 512; k < img.n; k += 128) {
                size_t len = img.n - k < 128 ? img.n - k : 128;
                uint8_t op = (uint8_t)(0x80 | (len - 1));
                st_bytes_append(&data, &op, 1);
                st_bytes_append(&data, img.p + k, len);
            }
        be32(e + 4, (uint32_t)at);
        be32(e + 8, (uint32_t)(data.n - 16 - at));
        st_bytes_append(&bcem, e, 12);
    }
    make_fork(&rsrc, 0x6263656Du, 128, bcem.p, bcem.n);
    CHECK(st_image_detect(data.p, data.n, rsrc.p, rsrc.n) == ST_IMG_NDIF);
    StBytes raw = {0};
    CHECK_RC(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err), ST_OK);
    CHECK(raw.n == img.n && !memcmp(raw.p, img.p, img.n));
    /* As delivered: a MacBinary file whose resource fork holds the bcem. */
    StMacFile w;
    st_macfile_init(&w);
    snprintf(w.name, sizeof w.name, "NDIF Disk.img");
    st_bytes_append(&w.data, data.p, data.n);
    st_bytes_append(&w.rsrc, rsrc.p, rsrc.n);
    StBytes mb = {0};
    CHECK_RC(st_macbinary_encode(&w, &mb, &err), ST_OK);
    StUnpackInfo info;
    CHECK_RC(st_unpack(mb.p, mb.n, NULL, 0, "x", &b, &info, NULL, &err), ST_OK);
    CHECK(b.n == 2 && !strcmp(info.chain, "MacBinary > NDIF disk image > HFS"));
    st_archive_free(&b);
    data.p[16 + 5] ^= 1; /* raw chunk damage: the image checksum catches it */
    CHECK_RC(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err), ST_ECHECKSUM);
    st_archive_free(&a);
    st_macfile_free(&w);
    st_bytes_free(&img); st_bytes_free(&data); st_bytes_free(&bcem); st_bytes_free(&rsrc);
    st_bytes_free(&raw); st_bytes_free(&mb);
}

int main(void) {
    test_ndif();
    char root[256];
    snprintf(root, sizeof root, "%s/os999-rt-%ld", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", root, root);
    CHECK(system(cmd) == 0);
    StVolume *v;
    StErr err;
    CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
    StMacFile app;
    st_macfile_init(&app);
    snprintf(app.name, sizeof app.name, "Game");
    st_mac_set_type_creator(&app, 0x4150504Cu, 0x47414D45u); /* APPL/GAME */
    static const char code[] = "\x4E\x75 jump table and code";
    make_fork(&app.rsrc, 0x434F4445u /* CODE */, 1, code, sizeof code - 1);
    CHECK_RC(st_vol_write(v, "Applications/Game/Game", &app, &err), ST_OK);
    /* Read the sidecar the way the desktop does. */
    char side[512];
    snprintf(side, sizeof side, "%s/Applications/Game/Game.rsrc", root);
    StBytes s = {0};
    CHECK_RC(st_read_file(side, 1 << 20, &s, &err), ST_OK);
    MacResourceFork fork;
    char msg[160];
    CHECK(mac_resource_open_container(&fork, s.p, s.n, msg, sizeof msg) == 0);
    MacResource res;
    CHECK(mac_resource_find(&fork, MAC_RESOURCE_TYPE('C', 'O', 'D', 'E'), 1, &res) == 0);
    CHECK(res.size == strlen("\x4E\x75 jump table and code") && res.data[0] == 0x4E);
    const uint8_t *d;
    size_t dn;
    CHECK(st_find_resource(app.rsrc.p, app.rsrc.n, 0x434F4445u, 1, &d, &dn) == 0 && dn == res.size);
    CHECK(st_find_resource(app.rsrc.p, app.rsrc.n, 0x434F4445u, 2, &d, &dn) != 0);
    st_bytes_free(&s);
    /* An install interrupted by a crash (manifest left behind) is undone
     * the next time the volume opens. */
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/New/Part' && echo x > '%s/New/Part/a' && echo y > '%s/New/Part/a.rsrc' && "
             "printf 'P New\\nP New/Part\\nF New/Part/a\\nF New/Part/b\\n' > '%s/.os999-install'", root, root, root, root);
    CHECK(system(cmd) == 0);
    st_vol_close(v);
    CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
    struct stat st;
    snprintf(side, sizeof side, "%s/New", root);
    CHECK(lstat(side, &st) != 0);
    snprintf(side, sizeof side, "%s/.os999-install", root);
    CHECK(lstat(side, &st) != 0);
    StVolumeUsage u;
    st_vol_usage(v, &u);
    CHECK(u.recovered >= 1 && u.files == 1);
    st_vol_close(v);
    st_macfile_free(&app);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) {}
    return st_test_done("test_storage_runtime");
}
