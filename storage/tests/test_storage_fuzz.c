/* Deterministic mutation fuzzing of every parser over valid samples.
 * Each mutant must decode cleanly or fail with an StStatus: the
 * sanitizer build turns any out-of-bounds read or leak into a failure.
 * OS999_STORAGE_FUZZ_ITERS overrides the per-format iteration count. */
#include "st_test.h"

#include <signal.h>
#include <unistd.h>
#include <zlib.h>

typedef int (*Decode)(const uint8_t *p, size_t n);

static int d_macbinary(const uint8_t *p, size_t n) { StMacFile f; int rc = st_macbinary_decode(p, n, &f, NULL, NULL); if (!rc) st_macfile_free(&f); return rc; }
static int d_binhex(const uint8_t *p, size_t n) { StMacFile f; int rc = st_binhex_decode(p, n, &f, NULL, NULL); if (!rc) st_macfile_free(&f); return rc; }
static int d_single(const uint8_t *p, size_t n) { StMacFile f; int rc = st_applesingle_decode(p, n, &f, NULL, NULL); if (!rc) st_macfile_free(&f); return rc; }
static int d_unpack(const uint8_t *p, size_t n) {
    StArchive a; StUnpackInfo i;
    StLimits l = st_limits_default();
    l.max_total = 64u << 20; l.max_entry = 32u << 20;
    int rc = st_unpack(p, n, NULL, 0, "fuzz", &a, &i, &l, NULL);
    if (!rc) st_archive_free(&a);
    return rc;
}
static int d_hfs(const uint8_t *p, size_t n) {
    StArchive a = {0}; char vn[64];
    StLimits l = st_limits_default();
    l.max_entry = 16u << 20;
    int rc = st_hfs_read(p, n, &a, vn, sizeof vn, &l, NULL);
    if (!rc) st_archive_free(&a);
    return rc;
}

int st_lsar_merge(const char *json, size_t n, StArchive *a);
static int d_lsar(const uint8_t *p, size_t n) {
    StArchive a = {0};
    StMacFile f;
    st_macfile_init(&f);
    st_archive_add(&a, "A/b\xc3\xa9", 0, &f, NULL, NULL);
    st_lsar_merge((const char *)p, n, &a);
    st_archive_free(&a);
    return ST_OK;
}

static int iters(void) {
    const char *e = getenv("OS999_STORAGE_FUZZ_ITERS");
    return e ? atoi(e) : 1500;
}

static void fuzz(const char *name, const StBytes *seed, Decode fn, uint32_t s0, int focus_head) {
    StBytes m = {0};
    uint32_t s = s0;
    int ok = 0, bad = 0, n = iters();
    CHECK(fn(seed->p, seed->n) == ST_OK);
    alarm(240); /* a hang kills the test instead of stalling the gate */
    for (int i = 0; i < n; i++) {
        m.n = 0;
        st_bytes_append(&m, seed->p, seed->n);
        int edits = 1 + (int)(st_rand(&s) % 8);
        for (int k = 0; k < edits; k++) {
            uint32_t r = st_rand(&s);
            size_t span = focus_head && (r & 1) ? (m.n < 2048 ? m.n : 2048) : m.n;
            size_t at = span ? st_rand(&s) % span : 0;
            switch (r % 5) {
            case 0: m.p[at] ^= (uint8_t)(1u << (st_rand(&s) % 8)); break;
            case 1: m.p[at] = (uint8_t)st_rand(&s); break;
            case 2: m.p[at] = (r & 8) ? 0xFF : 0x00; break;
            case 3: if (at + 4 <= m.n) { uint32_t v = st_rand(&s); memcpy(m.p + at, &v, 4); } break;
            case 4: m.n = at + 1; break; /* truncate */
            }
        }
        int rc = fn(m.p, m.n);
        if (rc == ST_OK) ok++;
        else if (rc < 0 && rc >= ST_EINVAL) bad++;
        else { CHECK(!"unexpected status"); }
    }
    alarm(0);
    printf("  %-14s %5d mutants: %d decoded, %d rejected\n", name, n, ok, bad);
    st_bytes_free(&m);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { put16(p, (uint16_t)(v >> 16)); put16(p + 2, (uint16_t)v); }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

/* Classic StuffIt: folder, stored file, RLE90 file, folder end. */
static void seed_sit(StBytes *a) {
    uint8_t hdr[22] = {'S', 'I', 'T', '!', 0, 2, 0, 0, 0, 0, 'r', 'L', 'a', 'u', 1};
    st_bytes_append(a, hdr, 22);
    const char *names[4] = {"Dir", "stored", "rle", "Dir"};
    const int meth[4] = {32, 0, 1, 33};
    const uint8_t plain[] = "aaaaaaaaab", rle[] = {'a', 0x90, 9, 'b'};
    for (int k = 0; k < 4; k++) {
        uint8_t h[112] = {0};
        h[0] = h[1] = (uint8_t)meth[k];
        h[2] = (uint8_t)strlen(names[k]);
        memcpy(h + 3, names[k], strlen(names[k]));
        memcpy(h + 66, "TEXTttxt", 8);
        int file = meth[k] < 32;
        if (file) {
            put32(h + 88, sizeof plain - 1);
            put32(h + 96, meth[k] ? sizeof rle : sizeof plain - 1);
            put16(h + 102, st_crc16_arc(0, plain, sizeof plain - 1));
        }
        put16(h + 110, st_crc16_arc(0, h, 110));
        st_bytes_append(a, h, 112);
        if (file) st_bytes_append(a, meth[k] ? rle : plain, meth[k] ? sizeof rle : sizeof plain - 1);
    }
}

/* StuffIt 5: one folder holding one stored file. */
static void seed_sit5(StBytes *a) {
    uint8_t h[114] = {0};
    memcpy(h, "StuffIt (c)1997-1998 Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n", 80);
    h[82] = 5;
    put32(h + 88, 114);
    st_bytes_append(a, h, 114);
    const uint8_t body[] = "five";
    for (int k = 0; k < 2; k++) {
        uint8_t e[52] = {0}, second[36] = {0};
        put32(e, 0xA5A5A5A5u);
        e[4] = 1;
        put16(e + 6, 49);
        e[9] = k ? 0x10 : 0x40;
        put32(e + 26, k ? 114 : 0);
        put16(e + 30, 1);
        put32(e + 34, k ? 4 : 114 + 49 + 36);
        put32(e + 38, k ? 4 : 0);
        put16(e + 42, st_crc16_arc(0, body, 4));
        e[48] = k ? 'f' : 'D';
        st_bytes_append(a, e, 49);
        st_bytes_append(a, second, 36);
        if (k) st_bytes_append(a, body, 4);
    }
}

/* UDIF: one raw and one zlib chunk over an HFS volume, plus koly. */
static void seed_udif(const StBytes *hfs, StBytes *out) {
    uint64_t sectors = hfs->n / 512, half = sectors / 2;
    uLongf zl = compressBound((uLong)(hfs->n - half * 512));
    uint8_t *z = malloc(zl);
    compress2(z, &zl, hfs->p + half * 512, (uLong)(hfs->n - half * 512), 9);
    st_bytes_append(out, hfs->p, (size_t)half * 512);
    st_bytes_append(out, z, zl);
    free(z);
    uint8_t m[204 + 3 * 40] = {0};
    memcpy(m, "mish", 4);
    put64(m + 16, sectors);
    put32(m + 200, 3);
    put32(m + 204, 1); put64(m + 204 + 16, half); put64(m + 204 + 32, half * 512);
    put32(m + 244, 0x80000005u); put64(m + 244 + 8, half); put64(m + 244 + 16, sectors - half);
    put64(m + 244 + 24, half * 512); put64(m + 244 + 32, zl);
    put32(m + 284, 0xFFFFFFFFu);
    static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint64_t xo = out->n;
    const char *head = "<plist><dict><key>blkx</key><array><dict><data>";
    st_bytes_append(out, head, strlen(head));
    for (size_t i = 0; i < sizeof m; i += 3) {
        uint32_t v = (uint32_t)m[i] << 16 | (i + 1 < sizeof m ? (uint32_t)m[i + 1] << 8 : 0) | (i + 2 < sizeof m ? m[i + 2] : 0);
        char q[4] = {b64[v >> 18], b64[(v >> 12) & 63], i + 1 < sizeof m ? b64[(v >> 6) & 63] : '=', i + 2 < sizeof m ? b64[v & 63] : '='};
        st_bytes_append(out, q, 4);
    }
    const char *tail = "</data></dict></array></dict></plist>";
    st_bytes_append(out, tail, strlen(tail));
    uint8_t k[512] = {0};
    memcpy(k, "koly", 4);
    put64(k + 216, xo);
    put64(k + 224, out->n - xo);
    put64(k + 492, sectors);
    st_bytes_append(out, k, 512);
}

int main(void) {
    StMacFile f;
    st_make_file(&f, "Fuzz Me", 3000, 2000, 7);
    StBytes mb = {0}, hqx = {0}, as = {0}, hfs = {0}, dc = {0};
    StErr err;
    st_macbinary_encode(&f, &mb, &err);
    st_binhex_encode(&f, &hqx, &err);
    st_applesingle_encode(&f, 0, &as, &err);
    StArchive a = {0};
    for (int i = 0; i < 40; i++) {
        StMacFile g;
        char p[64];
        snprintf(p, sizeof p, "%s/File %d", i % 2 ? "A" : "B/C", i);
        st_make_file(&g, p + (i % 2 ? 2 : 4), (size_t)i * 100, (size_t)i * 30, (uint32_t)i + 1);
        st_archive_add(&a, p, 0, &g, NULL, NULL);
    }
    CHECK_RC(st_hfs_write(&a, "Fuzz", 819200, &hfs, &err), ST_OK);
    CHECK_RC(st_dc42_encode(hfs.p, hfs.n, "Fuzz", &dc, &err), ST_OK);
    /* ZIP seed via the unpack path: a stored and a deflated member. */
    StBytes zip = {0};
    {
        const char *nm = "dir/file.txt";
        uint8_t body[] = "classic mac storage fuzzing seed classic mac storage";
        uint8_t comp[256];
        uLongf cl = sizeof comp;
        z_stream z;
        memset(&z, 0, sizeof z);
        deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
        z.next_in = body; z.avail_in = sizeof body; z.next_out = comp; z.avail_out = sizeof comp;
        deflate(&z, Z_FINISH);
        cl = z.total_out;
        deflateEnd(&z);
        uint32_t crc = st_crc32(0, body, sizeof body);
        uint8_t lh[30] = {0x50, 0x4b, 3, 4, 20, 0, 0, 0, 8, 0};
        memcpy(lh + 14, &crc, 4);
        uint32_t c32 = (uint32_t)cl, u32 = sizeof body;
        memcpy(lh + 18, &c32, 4); memcpy(lh + 22, &u32, 4);
        lh[26] = (uint8_t)strlen(nm);
        st_bytes_append(&zip, lh, 30); st_bytes_append(&zip, nm, strlen(nm)); st_bytes_append(&zip, comp, cl);
        uint8_t ch[46] = {0x50, 0x4b, 1, 2, 20, 0, 20, 0, 0, 0, 8, 0};
        memcpy(ch + 16, &crc, 4); memcpy(ch + 20, &c32, 4); memcpy(ch + 24, &u32, 4);
        ch[28] = (uint8_t)strlen(nm);
        uint32_t cdo = (uint32_t)zip.n;
        st_bytes_append(&zip, ch, 46); st_bytes_append(&zip, nm, strlen(nm));
        uint8_t eo[22] = {0x50, 0x4b, 5, 6, 0, 0, 0, 0, 1, 0, 1, 0};
        uint32_t cds = 46 + (uint32_t)strlen(nm);
        memcpy(eo + 12, &cds, 4); memcpy(eo + 16, &cdo, 4);
        st_bytes_append(&zip, eo, 22);
    }
    /* Compact Pro seed: one RLE-coded file inside a folder. */
    StBytes cpt = {0};
    {
        const uint8_t body[] = {'h', 'e', 'l', 'l', 'o', 'a', 0x81, 0x82, 6, '!'};
        const uint8_t plain[] = "helloaaaaaa!";
        uint8_t h[8] = {1, 1, 0, 0, 0, 0, 0, 8 + sizeof body};
        st_bytes_append(&cpt, h, 8);
        st_bytes_append(&cpt, body, sizeof body);
        uint8_t d[7] = {0, 0, 0, 0, 0, 2, 0}, fold[5] = {0x82, 'F', 'o', 0, 1};
        st_bytes_append(&cpt, d, 7);
        st_bytes_append(&cpt, fold, 5);
        uint8_t e[47];
        memset(e, 0, sizeof e);
        e[0] = 1; e[1] = 'x'; e[2] = 1; e[6] = 8;
        memcpy(e + 7, "TEXTttxt", 8);
        uint32_t crc = ~st_crc32(0, plain, sizeof plain - 1);
        for (int k = 0; k < 4; k++) e[25 + k] = (uint8_t)(crc >> (24 - 8 * k));
        e[38] = sizeof plain - 1;   /* data length (low byte) */
        e[46] = sizeof body;        /* data compressed length (low byte) */
        st_bytes_append(&cpt, e, sizeof e);
    }
    printf("mutation fuzzing (%d per format):\n", iters());
    fuzz("MacBinary", &mb, d_macbinary, 1, 1);
    fuzz("BinHex", &hqx, d_binhex, 2, 0);
    fuzz("AppleSingle", &as, d_single, 3, 1);
    fuzz("ZIP", &zip, d_unpack, 4, 0);
    fuzz("Compact Pro", &cpt, d_unpack, 8, 0);
    StBytes js = {0};
    const char *doc = "{\"lsarContents\": [ {\"XADFileName\": \"A/b\\u00e9\", \"XADFileType\": 1413830740, "
                      "\"XADFileCreator\": 1061109567, \"XADFinderFlags\": 256}, {\"XADFileName\": \"x\\ud83d\\ude00\"} ]}";
    st_bytes_append(&js, doc, strlen(doc));
    fuzz("lsar JSON", &js, d_lsar, 9, 0);
    StBytes sit = {0}, sit5 = {0}, udif = {0};
    seed_sit(&sit);
    seed_sit5(&sit5);
    seed_udif(&hfs, &udif);
    fuzz("StuffIt", &sit, d_unpack, 10, 0);
    fuzz("StuffIt 5", &sit5, d_unpack, 11, 0);
    fuzz("UDIF", &udif, d_unpack, 12, 0);
    st_bytes_free(&sit); st_bytes_free(&sit5); st_bytes_free(&udif);
    st_bytes_free(&js);
    fuzz("HFS volume", &hfs, d_hfs, 5, 0);
    fuzz("DiskCopy+HFS", &dc, d_unpack, 6, 1);
    /* HFS metadata areas are where the interesting structure lives. */
    StBytes meta = {0};
    st_bytes_append(&meta, hfs.p, hfs.n);
    fuzz("HFS (MDB)", &meta, d_hfs, 7, 1);
    st_macfile_free(&f);
    st_archive_free(&a);
    st_bytes_free(&mb); st_bytes_free(&hqx); st_bytes_free(&as); st_bytes_free(&hfs);
    st_bytes_free(&dc); st_bytes_free(&zip); st_bytes_free(&meta); st_bytes_free(&cpt);
    return st_test_done("test_storage_fuzz");
}
