/* HFS writer/reader round trips, Disk Copy 4.2, UDIF, NDIF, partition
 * maps (APM, GPT) and the unpack chain over nested wrappers. */
#include "st_test.h"

#include <zlib.h>

static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { be16(p, (uint16_t)(v >> 16)); be16(p + 2, (uint16_t)v); }
static uint16_t st_be16_test(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t st_be32_test(const uint8_t *p) { return (uint32_t)st_be16_test(p) << 16 | st_be16_test(p + 2); }
static void be64(uint8_t *p, uint64_t v) { be32(p, (uint32_t)(v >> 32)); be32(p + 4, (uint32_t)v); }

static const StEntry *find(const StArchive *a, const char *path) {
    for (size_t i = 0; i < a->n; i++) if (!strcmp(a->v[i].path, path)) return &a->v[i];
    return NULL;
}

/* An archive with nested folders and enough files to need several
 * catalog leaves and an index level. */
static void sample(StArchive *a, int files) {
    memset(a, 0, sizeof *a);
    StMacFile d;
    st_macfile_init(&d);
    snprintf(d.name, sizeof d.name, "System Folder");
    st_archive_add(a, "System Folder", 1, &d, NULL, NULL);
    for (int i = 0; i < files; i++) {
        StMacFile f;
        char name[64], path[128];
        snprintf(name, sizeof name, "File %03d", i);
        snprintf(path, sizeof path, "%s%s", i % 3 ? "Games/Arcade/" : "System Folder/", name);
        st_make_file(&f, name, (size_t)(i * 37) % 5000, (size_t)(i * 91) % 3000, (uint32_t)i + 1);
        st_archive_add(a, path, 0, &f, NULL, NULL);
    }
    StMacFile r;
    st_make_file(&r, "Read Me", 10, 0, 999);
    st_archive_add(a, "Read Me", 0, &r, NULL, NULL);
}

static int same_archive(const StArchive *want, const StArchive *got) {
    int ok = 1;
    for (size_t i = 0; i < want->n; i++) {
        const StEntry *g = find(got, want->v[i].path);
        if (!g || g->is_dir != want->v[i].is_dir) { fprintf(stderr, "missing %s\n", want->v[i].path); ok = 0; continue; }
        if (!g->is_dir && (!st_same_file(&want->v[i].file, &g->file) || g->file.mtime != want->v[i].file.mtime)) {
            fprintf(stderr, "differs %s\n", want->v[i].path);
            ok = 0;
        }
    }
    return ok;
}

static void test_hfs_roundtrip(void) {
    StArchive a, b = {0};
    sample(&a, 120);
    StBytes img = {0};
    StErr err;
    CHECK_RC(st_hfs_write(&a, "Test Disk", 4 * 1024 * 1024, &img, &err), ST_OK);
    size_t off, len;
    CHECK_RC(st_find_hfs(img.p, img.n, &off, &len, &err), ST_OK);
    char vol[64];
    CHECK_RC(st_hfs_read(img.p + off, len, &b, vol, sizeof vol, NULL, &err), ST_OK);
    CHECK(!strcmp(vol, "Test Disk"));
    CHECK(same_archive(&a, &b));
    CHECK(find(&b, "Games") && find(&b, "Games/Arcade") && find(&b, "Games/Arcade")->is_dir);
    FILE *keep = getenv("OS999_STORAGE_DUMP") ? fopen(getenv("OS999_STORAGE_DUMP"), "wb") : NULL;
    if (keep) { fwrite(img.p, 1, img.n, keep); fclose(keep); }
    st_archive_free(&b);
    /* Too small a volume is refused before anything is written. */
    CHECK_RC(st_hfs_write(&a, "Tiny", 409600, &img, &err), ST_ELIMIT);
    /* Duplicate names differing only in case cannot share an HFS folder. */
    StArchive dup = {0};
    StMacFile x, y;
    st_make_file(&x, "Same", 1, 0, 1);
    st_make_file(&y, "SAME", 1, 0, 2);
    st_archive_add(&dup, "Same", 0, &x, NULL, NULL);
    st_archive_add(&dup, "SAME", 0, &y, NULL, NULL);
    CHECK_RC(st_hfs_write(&dup, "Dup", 819200, &img, &err), ST_EEXIST);
    st_archive_free(&dup);
    st_archive_free(&a);
    st_bytes_free(&img);
}

static void test_dc42(void) {
    StArchive a, b = {0};
    sample(&a, 10);
    StBytes img = {0}, dc = {0}, raw = {0};
    StErr err;
    CHECK_RC(st_hfs_write(&a, "Floppy", 819200, &img, &err), ST_OK);
    CHECK_RC(st_dc42_encode(img.p, img.n, "Floppy", &dc, &err), ST_OK);
    CHECK(st_image_detect(dc.p, dc.n, NULL, 0) == ST_IMG_DC42);
    CHECK_RC(st_image_decode(dc.p, dc.n, NULL, 0, &raw, NULL, &err), ST_OK);
    CHECK(raw.n == img.n && !memcmp(raw.p, img.p, img.n));
    dc.p[84 + 5000] ^= 1;
    CHECK_RC(st_image_decode(dc.p, dc.n, NULL, 0, &raw, NULL, &err), ST_ECHECKSUM);
    dc.p[84 + 5000] ^= 1;
    StUnpackInfo info;
    CHECK_RC(st_unpack(dc.p, dc.n, NULL, 0, "x", &b, &info, NULL, &err), ST_OK);
    CHECK(same_archive(&a, &b) && !strcmp(info.volume, "Floppy"));
    CHECK(strstr(info.chain, "Disk Copy 4.2") && strstr(info.chain, "HFS"));
    st_archive_free(&a); st_archive_free(&b);
    st_bytes_free(&img); st_bytes_free(&dc); st_bytes_free(&raw);
}

/* UDIF: koly trailer + XML plist + one mish table using zlib, ADC
 * (literal-only), zero-fill and raw chunks. */
static void build_udif(const StBytes *raw, StBytes *out) {
    uint64_t sectors = raw->n / 512, per = sectors / 4;
    StBytes data = {0}, mish = {0};
    uint8_t m[204 + 5 * 40];
    memset(m, 0, sizeof m);
    memcpy(m, "mish", 4);
    be32(m + 4, 1);
    be64(m + 16, sectors);
    be32(m + 200, 5);
    for (int c = 0; c < 4; c++) {
        uint64_t s0 = per * (uint64_t)c, cnt = c == 3 ? sectors - s0 : per;
        const uint8_t *src = raw->p + s0 * 512;
        size_t sn = (size_t)cnt * 512;
        uint8_t *e = m + 204 + c * 40;
        uint32_t type = c == 0 ? 0x80000005u : c == 1 ? 0x80000004u : c == 2 ? 1u : 0x80000005u;
        be32(e, type);
        be64(e + 8, s0);
        be64(e + 16, cnt);
        be64(e + 24, data.n);
        if (type == 0x80000005u) {
            uLongf cl = compressBound((uLong)sn);
            st_bytes_reserve(&data, data.n + cl);
            compress2(data.p + data.n, &cl, src, (uLong)sn, 9);
            data.n += cl;
            be64(e + 32, cl);
        } else if (type == 0x80000004u) {
            size_t before = data.n;
            for (size_t i = 0; i < sn; i += 128) {
                size_t k = sn - i < 128 ? sn - i : 128;
                uint8_t op = (uint8_t)(0x80 | (k - 1));
                st_bytes_append(&data, &op, 1);
                st_bytes_append(&data, src + i, k);
            }
            be64(e + 32, data.n - before);
        } else {
            st_bytes_append(&data, src, sn);
            be64(e + 32, sn);
        }
    }
    be32(m + 204 + 4 * 40, 0xFFFFFFFFu);
    st_bytes_append(&mish, m, sizeof m);
    /* base64 */
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    StBytes xml = {0};
    const char *head = "<?xml version=\"1.0\"?><plist><dict><key>resource-fork</key><dict><key>blkx</key><array><dict><key>Data</key><data>\n";
    st_bytes_append(&xml, head, strlen(head));
    for (size_t i = 0; i < mish.n; i += 3) {
        uint32_t v = (uint32_t)mish.p[i] << 16 | (i + 1 < mish.n ? (uint32_t)mish.p[i + 1] << 8 : 0) | (i + 2 < mish.n ? mish.p[i + 2] : 0);
        char q[4] = {b64[v >> 18], b64[(v >> 12) & 63], i + 1 < mish.n ? b64[(v >> 6) & 63] : '=', i + 2 < mish.n ? b64[v & 63] : '='};
        st_bytes_append(&xml, q, 4);
    }
    const char *tail = "\n</data></dict></array></dict></dict></plist>";
    st_bytes_append(&xml, tail, strlen(tail));
    out->n = 0;
    st_bytes_append(out, data.p, data.n);
    uint64_t xo = out->n;
    st_bytes_append(out, xml.p, xml.n);
    uint8_t k[512];
    memset(k, 0, sizeof k);
    memcpy(k, "koly", 4);
    be32(k + 4, 4);
    be32(k + 8, 512);
    be64(k + 32, data.n);
    be64(k + 216, xo);
    be64(k + 224, xml.n);
    be64(k + 492, sectors);
    st_bytes_append(out, k, 512);
    st_bytes_free(&data); st_bytes_free(&mish); st_bytes_free(&xml);
}

static void test_udif_and_maps(void) {
    StArchive a, b = {0};
    sample(&a, 30);
    StBytes img = {0}, dmg = {0}, raw = {0}, disk = {0};
    StErr err;
    CHECK_RC(st_hfs_write(&a, "Installer", 2 * 1024 * 1024, &img, &err), ST_OK);
    build_udif(&img, &dmg);
    CHECK(st_image_detect(dmg.p, dmg.n, NULL, 0) == ST_IMG_UDIF);
    CHECK_RC(st_image_decode(dmg.p, dmg.n, NULL, 0, &raw, NULL, &err), ST_OK);
    CHECK(raw.n == img.n && !memcmp(raw.p, img.p, img.n));
    /* Apple partition map: DDM, two map entries, HFS at block 64. */
    st_bytes_reserve(&disk, 64 * 512 + img.n);
    memset(disk.p, 0, 64 * 512);
    disk.n = 64 * 512;
    be16(disk.p, 0x4552);
    be16(disk.p + 2, 512);
    for (int i = 0; i < 2; i++) {
        uint8_t *pm = disk.p + 512 * (i + 1);
        be16(pm, 0x504D);
        be32(pm + 4, 2);
        be32(pm + 8, i ? 64 : 1);
        be32(pm + 12, i ? (uint32_t)(img.n / 512) : 63);
        memcpy(pm + 48, i ? "Apple_HFS" : "Apple_partition_map", i ? 9 : 19);
    }
    st_bytes_append(&disk, img.p, img.n);
    size_t off, len;
    CHECK_RC(st_find_hfs(disk.p, disk.n, &off, &len, &err), ST_OK);
    CHECK(off == 64 * 512 && len == img.n);
    StUnpackInfo info;
    CHECK_RC(st_unpack(disk.p, disk.n, NULL, 0, "cd", &b, &info, NULL, &err), ST_OK);
    CHECK(same_archive(&a, &b));
    st_archive_free(&b);
    /* GPT: protective MBR, header at LBA 1, entries at LBA 2, HFS at 40. */
    disk.n = 0;
    st_bytes_reserve(&disk, 40 * 512 + img.n);
    memset(disk.p, 0, 40 * 512);
    disk.n = 40 * 512;
    memcpy(disk.p + 512, "EFI PART", 8);
    disk.p[512 + 72] = 2;
    disk.p[512 + 80] = 1;
    disk.p[512 + 84] = 128;
    uint8_t *e = disk.p + 1024;
    e[32] = 40;
    uint64_t last = 40 + img.n / 512 - 1;
    for (int i = 0; i < 8; i++) e[40 + i] = (uint8_t)(last >> (8 * i));
    st_bytes_append(&disk, img.p, img.n);
    CHECK_RC(st_find_hfs(disk.p, disk.n, &off, &len, &err), ST_OK);
    CHECK(off == 40 * 512);
    /* Chain: MacBinary(BinHex? no) — MacBinary wrapping the UDIF. */
    StMacFile w;
    st_macfile_init(&w);
    snprintf(w.name, sizeof w.name, "Installer.dmg");
    st_bytes_append(&w.data, dmg.p, dmg.n);
    StBytes mb = {0}, hqx = {0};
    CHECK_RC(st_macbinary_encode(&w, &mb, &err), ST_OK);
    StMacFile w2;
    st_macfile_init(&w2);
    snprintf(w2.name, sizeof w2.name, "Installer.dmg.bin");
    st_bytes_append(&w2.data, mb.p, mb.n);
    CHECK_RC(st_binhex_encode(&w2, &hqx, &err), ST_OK);
    CHECK_RC(st_unpack(hqx.p, hqx.n, NULL, 0, "x.hqx", &b, &info, NULL, &err), ST_OK);
    CHECK(same_archive(&a, &b));
    CHECK(!strcmp(info.chain, "BinHex > MacBinary > UDIF disk image > HFS"));
    st_archive_free(&b);
    /* Truncated trailer data is an error, not a crash. */
    dmg.p[dmg.n - 512 + 216] = 0x7F; /* XML offset far past the end */
    CHECK(st_image_decode(dmg.p, dmg.n, NULL, 0, &raw, NULL, &err) != ST_OK);
    st_macfile_free(&w); st_macfile_free(&w2);
    st_bytes_free(&mb); st_bytes_free(&hqx);
    st_archive_free(&a);
    st_bytes_free(&img); st_bytes_free(&dmg); st_bytes_free(&raw); st_bytes_free(&disk);
}

/* Split one file's data fork across an extents-overflow record: the
 * catalog keeps three one-block extents and the rest moves to a leaf
 * record in the extents B-tree, the way a fragmented volume stores it. */
static void test_hfs_overflow(void) {
    StArchive a = {0}, b = {0};
    StMacFile big;
    st_make_file(&big, "Fragmented", 20000, 100, 77);
    st_archive_add(&a, "Fragmented", 0, &big, NULL, NULL);
    StBytes img = {0};
    StErr err;
    CHECK_RC(st_hfs_write(&a, "Frag", 819200, &img, &err), ST_OK);
    uint8_t *m = img.p + 1024;
    uint32_t alsz = st_be32_test(m + 20);
    uint8_t *vol = img.p + (size_t)st_be16_test(m + 28) * 512;
    uint8_t *xt = vol + (size_t)st_be16_test(m + 134) * alsz;
    uint8_t *cat = vol + (size_t)st_be16_test(m + 150) * alsz;
    /* Find the file record in the catalog leaf chain. */
    uint8_t *rec = NULL;
    for (uint32_t node = st_be32_test(cat + 14 + 10); node && !rec; node = st_be32_test(cat + node * 512)) {
        uint8_t *nd = cat + node * 512;
        for (int r = 0; r < st_be16_test(nd + 10); r++) {
            uint8_t *k = nd + st_be16_test(nd + 512 - 2 * (r + 1));
            uint8_t *d = k + ((1 + k[0] + 1) & ~1);
            if (d[0] == 2 && k[6] == 10 && !memcmp(k + 7, "Fragmented", 10)) rec = d;
        }
    }
    CHECK(rec != NULL);
    if (!rec) return;
    uint32_t id = st_be32_test(rec + 20), start = st_be16_test(rec + 74), blocks = st_be16_test(rec + 76);
    CHECK(blocks >= 5);
    for (int e = 0; e < 3; e++) { be16(rec + 74 + 4 * e, (uint16_t)(start + e)); be16(rec + 76 + 4 * e, 1); }
    /* Extents leaf node 1: key (fork 0, id, start block 3) -> rest. */
    uint8_t *leaf = xt + 512;
    memset(leaf, 0, 512);
    leaf[8] = 0xFF;
    leaf[9] = 1;
    be16(leaf + 10, 1);
    uint8_t r[20] = {7, 0};
    be32(r + 2, id);
    be16(r + 6, 3);
    be16(r + 8, (uint16_t)(start + 3));
    be16(r + 10, (uint16_t)(blocks - 3));
    memcpy(leaf + 14, r, 20);
    be16(leaf + 510, 14);
    be16(leaf + 508, 34);
    uint8_t *hdr = xt + 14;
    be16(hdr, 1);
    be32(hdr + 2, 1);
    be32(hdr + 6, 1);
    be32(hdr + 10, 1);
    be32(hdr + 14, 1);
    char vn[32];
    CHECK_RC(st_hfs_read(img.p, img.n, &b, vn, sizeof vn, NULL, &err), ST_OK);
    const StEntry *f = find(&b, "Fragmented");
    CHECK(f && st_same_file(&a.v[0].file, &f->file) && f->file.data.n == 20000);
    st_archive_free(&b);
    /* Remove the overflow record: the fork is now missing extents. */
    be16(leaf + 10, 0);
    CHECK_RC(st_hfs_read(img.p, img.n, &b, vn, sizeof vn, NULL, &err), ST_EFORMAT);
    FILE *keep = getenv("OS999_STORAGE_DUMP_FRAG") ? fopen(getenv("OS999_STORAGE_DUMP_FRAG"), "wb") : NULL;
    if (keep) { be16(leaf + 10, 1); fwrite(img.p, 1, img.n, keep); fclose(keep); }
    st_archive_free(&a);
    st_bytes_free(&img);
}

static void test_plain_and_limits(void) {
    StArchive b = {0};
    StUnpackInfo info;
    StErr err;
    CHECK_RC(st_unpack((const uint8_t *)"just text", 9, NULL, 0, "notes", &b, &info, NULL, &err), ST_OK);
    CHECK(b.n == 1 && !strcmp(b.v[0].path, "notes") && info.chain[0] == 0);
    st_archive_free(&b);
    StLimits lim = st_limits_default();
    lim.max_depth = 1;
    StMacFile w;
    st_make_file(&w, "a", 10, 0, 1);
    StBytes m1 = {0}, m2 = {0};
    st_macbinary_encode(&w, &m1, &err);
    st_bytes_free(&w.data);
    st_bytes_append(&w.data, m1.p, m1.n);
    st_macbinary_encode(&w, &m2, &err);
    st_bytes_free(&w.data);
    st_bytes_append(&w.data, m2.p, m2.n);
    st_macbinary_encode(&w, &m1, &err);
    CHECK_RC(st_unpack(m1.p, m1.n, NULL, 0, "a", &b, &info, &lim, &err), ST_ELIMIT);
    st_macfile_free(&w);
    st_bytes_free(&m1); st_bytes_free(&m2);
}

/* ---- NDIF (Disk Copy 6) variants, and the failures Ian hit ---- */
static void make_fork(StBytes *f, uint32_t type, int16_t id, const void *payload, size_t pl) {
    size_t data_len = 4 + pl, map_len = 28 + 2 + 8 + 12;
    f->n = 0;
    st_bytes_reserve(f, 256 + data_len + map_len);
    memset(f->p, 0, 256 + data_len + map_len);
    f->n = 256 + data_len + map_len;
    uint8_t *h = f->p, *m = h + 256 + data_len;
    be32(h, 256); be32(h + 4, (uint32_t)(256 + data_len));
    be32(h + 8, (uint32_t)data_len); be32(h + 12, (uint32_t)map_len);
    be32(h + 256, (uint32_t)pl);
    memcpy(h + 260, payload, pl);
    memcpy(m, h, 16);
    be16(m + 24, 28); be16(m + 26, (uint16_t)map_len);
    be32(m + 30, type); be16(m + 36, 10);
    be16(m + 38, (uint16_t)id); be16(m + 40, 0xFFFF);
}

static uint32_t dc6_crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1) ? (crc >> 1) ^ 0x04C11DB7u : crc >> 1;
    }
    return crc;
}

/* chunks: 2 raw (blocks 0..z), 0 zero (z..z+8), 0x83 ADC literal runs, or
 * `kind` forced onto the last chunk (0x80 KenCode, 0x81 unknown). */
static void build_ndif(const StBytes *img, uint32_t zs, uint8_t last_type, StBytes *data, StBytes *rsrc) {
    StBytes bcem = {0};
    uint32_t sectors = (uint32_t)(img->n / 512);
    uint8_t hdr[128] = {0};
    hdr[1] = 10; hdr[4] = 4; memcpy(hdr + 5, "Test", 4);
    be32(hdr + 68, sectors);
    be32(hdr + 80, dc6_crc(img->p, img->n));
    be32(hdr + 124, 4);
    st_bytes_append(&bcem, hdr, 128);
    uint32_t starts[4] = {0, zs, zs + 8, sectors};
    uint8_t types[4] = {2, 0, last_type, 0xFF};
    data->n = 0;
    for (int c = 0; c < 4; c++) {
        uint8_t e[12] = {(uint8_t)(starts[c] >> 16), (uint8_t)(starts[c] >> 8), (uint8_t)starts[c], types[c]};
        size_t at = data->n;
        if (types[c] == 2) st_bytes_append(data, img->p, (size_t)zs * 512);
        if (types[c] >= 0x80 && types[c] != 0xFF)
            for (size_t k = (size_t)(zs + 8) * 512; k < img->n; k += 128) {
                size_t len = img->n - k < 128 ? img->n - k : 128;
                uint8_t op = (uint8_t)(0x80 | (len - 1));
                st_bytes_append(data, &op, 1);
                st_bytes_append(data, img->p + k, len);
            }
        be32(e + 4, (uint32_t)at);
        be32(e + 8, (uint32_t)(data->n - at));
        st_bytes_append(&bcem, e, 12);
    }
    make_fork(rsrc, 0x6263656Du, 128, bcem.p, bcem.n);
    st_bytes_free(&bcem);
}

static void test_ndif_variants(void) {
    StArchive a = {0}, b = {0};
    StMacFile f;
    st_make_file(&f, "Setup", 30000, 500, 7);
    st_archive_add(&a, "Install/Setup", 0, &f, NULL, NULL);
    StBytes img = {0}, data = {0}, rsrc = {0}, raw = {0};
    StErr err = {0};
    StUnpackInfo info;
    CHECK_RC(st_hfs_write(&a, "Install Disk", 819200, &img, &err), ST_OK);
    uint32_t zs = 0;
    for (uint32_t s = 200; s + 8 < 1500 && !zs; s++) {
        int zero = 1;
        for (size_t k = (size_t)s * 512; k < (size_t)(s + 8) * 512 && zero; k++) zero = !img.p[k];
        if (zero) zs = s;
    }
    CHECK(zs > 0);
    /* The compressed data fork starts like a plain HFS volume, so without
     * its bcem map it passes for one. That must now say what is wrong. */
    build_ndif(&img, zs, 0x83, &data, &rsrc);
    CHECK(st_image_detect(data.p, data.n, rsrc.p, rsrc.n) == ST_IMG_NDIF);
    CHECK_RC(st_unpack(data.p, data.n, rsrc.p, rsrc.n, "x.img", &b, &info, NULL, &err), ST_OK);
    CHECK(strstr(info.chain, "NDIF") && strstr(info.chain, "HFS") && !strcmp(info.volume, "Install Disk"));
    st_archive_free(&b);
    memset(&err, 0, sizeof err);
    CHECK_RC(st_unpack(data.p, 40 * 512, NULL, 0, "x.img", &b, &info, NULL, &err), ST_EFORMAT);
    CHECK(strstr(err.msg, "NDIF") && strstr(err.msg, "bcem") && !strstr(err.msg, "B-tree header"));
    /* Compression this build cannot decode names the format and chunk. */
    memset(&err, 0, sizeof err);
    build_ndif(&img, zs, 0x80, &data, &rsrc);
    CHECK_RC(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err), ST_EUNSUPPORTED);
    CHECK(strstr(err.msg, "NDIF (Disk Copy 6)") && strstr(err.msg, "KenCode"));
    memset(&err, 0, sizeof err);
    build_ndif(&img, zs, 0x81, &data, &rsrc);
    CHECK_RC(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err), ST_EUNSUPPORTED);
    CHECK(strstr(err.msg, "NDIF (Disk Copy 6)") && strstr(err.msg, "0x81"));
    /* Corrupt images: a chunk that points past the data, a lying count, and a
     * damaged ADC stream all fail cleanly. */
    build_ndif(&img, zs, 0x83, &data, &rsrc);
    uint8_t *bc = rsrc.p + 256 + 4;
    be32(bc + 128 + 12 * 2 + 8, 0x7FFFFFF0u); /* ADC length beyond the fork */
    CHECK_RC(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err), ST_EFORMAT);
    build_ndif(&img, zs, 0x83, &data, &rsrc);
    bc = rsrc.p + 256 + 4;
    be32(bc + 124, 4000);
    CHECK(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err) != ST_OK);
    build_ndif(&img, zs, 0x83, &data, &rsrc);
    data.p[data.n - 200] = 0x40; /* turns a literal run into a copy opcode */
    CHECK(st_image_decode(data.p, data.n, rsrc.p, rsrc.n, &raw, NULL, &err) != ST_OK);
    /* Disk Copy 4.2 around the same volume, and a bare volume behind a
     * 512-byte header. */
    StBytes dc = {0}, hdr512 = {0};
    CHECK_RC(st_dc42_encode(img.p, img.n, "Install Disk", &dc, &err), ST_OK);
    CHECK_RC(st_unpack(dc.p, dc.n, NULL, 0, "x.dc42", &b, &info, NULL, &err), ST_OK);
    CHECK(!strcmp(info.chain, "Disk Copy 4.2 > HFS"));
    st_archive_free(&b);
    uint8_t pad[512] = {0};
    st_bytes_append(&hdr512, pad, 512);
    st_bytes_append(&hdr512, img.p, img.n);
    CHECK_RC(st_unpack(hdr512.p, hdr512.n, NULL, 0, "x.img", &b, &info, NULL, &err), ST_OK);
    CHECK(!strcmp(info.volume, "Install Disk"));
    st_archive_free(&b);
    st_archive_free(&a);
    st_bytes_free(&img); st_bytes_free(&data); st_bytes_free(&rsrc); st_bytes_free(&raw);
    st_bytes_free(&dc); st_bytes_free(&hdr512);
}

int main(void) {
    test_hfs_roundtrip();
    test_dc42();
    test_udif_and_maps();
    test_hfs_overflow();
    test_plain_and_limits();
    test_ndif_variants();
    return st_test_done("test_storage_images");
}
