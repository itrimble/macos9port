/* ZIP and StuffIt archives built in the test, then decoded. */
#include "st_test.h"

#include <zlib.h>

static void le16(StBytes *b, uint16_t v) { uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; st_bytes_append(b, t, 2); }
static void le32(StBytes *b, uint32_t v) { le16(b, (uint16_t)v); le16(b, (uint16_t)(v >> 16)); }
static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) { be16(p, (uint16_t)(v >> 16)); be16(p + 2, (uint16_t)v); }

/* ---- ZIP ---- */
typedef struct { const char *name; const uint8_t *p; size_t n; int deflate; uint16_t flags; } ZEnt;

static void zip_build(StBytes *z, const ZEnt *e, int count) {
    StBytes cd = {0};
    z->n = 0;
    for (int i = 0; i < count; i++) {
        uint32_t crc = st_crc32(0, e[i].p, e[i].n), off = (uint32_t)z->n;
        uint8_t comp[70000];
        size_t clen = e[i].n;
        const uint8_t *body = e[i].p;
        if (e[i].deflate) {
            z_stream s;
            memset(&s, 0, sizeof s);
            deflateInit2(&s, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            s.next_in = (Bytef *)e[i].p; s.avail_in = (uInt)e[i].n;
            s.next_out = comp; s.avail_out = sizeof comp;
            deflate(&s, Z_FINISH);
            clen = s.total_out;
            deflateEnd(&s);
            body = comp;
        }
        uint16_t nl = (uint16_t)strlen(e[i].name);
        le32(z, 0x04034b50u); le16(z, 20); le16(z, e[i].flags); le16(z, e[i].deflate ? 8 : 0);
        le32(z, 0); le32(z, crc); le32(z, (uint32_t)clen); le32(z, (uint32_t)e[i].n);
        le16(z, nl); le16(z, 0);
        st_bytes_append(z, e[i].name, nl);
        st_bytes_append(z, body, clen);
        le32(&cd, 0x02014b50u); le16(&cd, 20); le16(&cd, 20); le16(&cd, e[i].flags); le16(&cd, e[i].deflate ? 8 : 0);
        le32(&cd, 0); le32(&cd, crc); le32(&cd, (uint32_t)clen); le32(&cd, (uint32_t)e[i].n);
        le16(&cd, nl); le16(&cd, 0); le16(&cd, 0); le16(&cd, 0); le16(&cd, 0); le32(&cd, 0); le32(&cd, off);
        st_bytes_append(&cd, e[i].name, nl);
    }
    uint32_t cdo = (uint32_t)z->n;
    st_bytes_append(z, cd.p, cd.n);
    le32(z, 0x06054b50u); le16(z, 0); le16(z, 0); le16(z, (uint16_t)count); le16(z, (uint16_t)count);
    le32(z, (uint32_t)cd.n); le32(z, cdo); le16(z, 0);
    st_bytes_free(&cd);
}

static const StEntry *find(const StArchive *a, const char *path) {
    for (size_t i = 0; i < a->n; i++) if (!strcmp(a->v[i].path, path)) return &a->v[i];
    return NULL;
}

static void test_zip(void) {
    StMacFile f;
    st_make_file(&f, "Game", 3000, 1200, 11);
    StBytes ad = {0}, z = {0};
    StErr err;
    CHECK_RC(st_applesingle_encode(&f, 1, &ad, &err), ST_OK);
    ZEnt e[] = {
        {"Folder/", (const uint8_t *)"", 0, 0, 0},
        {"Folder/Game", f.data.p, f.data.n, 1, 0},
        {"__MACOSX/Folder/._Game", ad.p, ad.n, 1, 0},
        {"notes.txt", (const uint8_t *)"hello", 5, 0, 0x800},
    };
    zip_build(&z, e, 4);
    CHECK(st_zip_detect(z.p, z.n));
    StArchive a = {0};
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_OK);
    CHECK(a.n == 3);
    const StEntry *g = find(&a, "Folder/Game");
    CHECK(g && st_same_file(&f, &g->file));
    CHECK(find(&a, "Folder") && find(&a, "Folder")->is_dir);
    CHECK(find(&a, "notes.txt") && find(&a, "notes.txt")->file.data.n == 5);
    st_archive_free(&a);
    /* Traversal and absolute names. */
    ZEnt evil[] = {{"../evil", (const uint8_t *)"x", 1, 0, 0}};
    zip_build(&z, evil, 1);
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_EPATH);
    ZEnt evil2[] = {{"a/../../evil", (const uint8_t *)"x", 1, 0, 0}};
    zip_build(&z, evil2, 1);
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_EPATH);
    ZEnt abs[] = {{"/etc/passwd", (const uint8_t *)"x", 1, 0, 0}};
    zip_build(&z, abs, 1);
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_EPATH); /* refused, not made relative (11 Nov 2026) */
    ZEnt enc[] = {{"secret", (const uint8_t *)"x", 1, 0, 1}};
    zip_build(&z, enc, 1);
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_EUNSUPPORTED);
    ZEnt ok1[] = {{"crc", (const uint8_t *)"abcdef", 6, 0, 0}};
    zip_build(&z, ok1, 1);
    z.p[30 + 3] ^= 1; /* corrupt stored data */
    CHECK_RC(st_zip_decode(z.p, z.n, &a, NULL, &err), ST_ECHECKSUM);
    StLimits lim = st_limits_default();
    lim.max_entries = 2;
    zip_build(&z, e, 4);
    CHECK_RC(st_zip_decode(z.p, z.n, &a, &lim, &err), ST_ELIMIT);
    st_bytes_free(&z);
    st_bytes_free(&ad);
    st_macfile_free(&f);
}

/* ---- StuffIt encoders for the test ---- */
static void enc_rle90(const StBytes *in, StBytes *out) {
    out->n = 0;
    for (size_t i = 0; i < in->n;) {
        uint8_t b = in->p[i];
        size_t run = 1;
        while (i + run < in->n && in->p[i + run] == b && run < 255) run++;
        uint8_t lit[2] = {b, 0};
        st_bytes_append(out, lit, b == 0x90 ? 2 : 1);
        if (run > 2) { uint8_t m[2] = {0x90, (uint8_t)run}; st_bytes_append(out, m, 2); i += run; }
        else i++;
    }
}

typedef struct { StBytes *o; uint32_t acc; int bits; } BitW;
static void bw_msb(BitW *w, uint32_t v, int n) {
    for (int i = n - 1; i >= 0; i--) {
        w->acc = (w->acc << 1) | ((v >> i) & 1);
        if (++w->bits == 8) { uint8_t b = (uint8_t)w->acc; st_bytes_append(w->o, &b, 1); w->acc = 0; w->bits = 0; }
    }
}
static void bw_flush(BitW *w) { while (w->bits) bw_msb(w, 0, 1); }

/* Huffman: a fixed balanced tree over the bytes present (valid prefix code). */
static void huff_tree(BitW *w, const int *syms, int lo, int hi) {
    if (lo == hi) { bw_msb(w, 1, 1); bw_msb(w, (uint32_t)syms[lo], 8); return; }
    bw_msb(w, 0, 1);
    int mid = (lo + hi) / 2;
    huff_tree(w, syms, lo, mid);
    huff_tree(w, syms, mid + 1, hi);
}
static void enc_huffman(const StBytes *in, StBytes *out) {
    int present[256] = {0}, syms[256], ns = 0;
    for (size_t i = 0; i < in->n; i++) present[in->p[i]] = 1;
    for (int c = 0; c < 256; c++) if (present[c]) syms[ns++] = c;
    if (ns == 1) syms[ns++] = syms[0] ^ 1;
    out->n = 0;
    BitW w = {out, 0, 0};
    huff_tree(&w, syms, 0, ns - 1);
    for (size_t i = 0; i < in->n; i++) {
        int lo = 0, hi = ns - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            int idx = 0;
            while (syms[idx] != in->p[i]) idx++;
            if (idx <= mid) { bw_msb(&w, 0, 1); hi = mid; } else { bw_msb(&w, 1, 1); lo = mid + 1; }
        }
    }
    bw_flush(&w);
}

/* LZW, compress(1) style: LSB-first codes, 9..14 bits, groups of eight. */
static void enc_lzw(const StBytes *in, StBytes *out) {
    static int32_t dict[1 << 14][256];
    memset(dict, 0xFF, sizeof dict);
    out->n = 0;
    uint64_t acc = 0;
    int nacc = 0, bits = 9, next = 257, cur = -1;
    size_t in_group = 0;
    size_t group_bytes = 0;
    for (size_t i = 0; i <= in->n; i++) {
        int c = i < in->n ? in->p[i] : -1;
        if (cur >= 0 && c >= 0 && dict[cur][c] >= 0) { cur = dict[cur][c]; continue; }
        if (cur >= 0) {
            acc |= (uint64_t)cur << nacc;
            nacc += bits;
            in_group++;
            while (nacc >= 8) { uint8_t b = (uint8_t)acc; st_bytes_append(out, &b, 1); acc >>= 8; nacc -= 8; group_bytes++; }
            if (c >= 0 && next < (1 << 14)) dict[cur][c] = next++;
            if (next > (1 << bits) && bits < 14) {
                /* pad to the end of this group of eight codes */
                while (in_group % 8) { acc |= 0; nacc += bits; in_group++; while (nacc >= 8) { uint8_t b = (uint8_t)acc; st_bytes_append(out, &b, 1); acc >>= 8; nacc -= 8; } }
                if (nacc) { uint8_t b = (uint8_t)acc; st_bytes_append(out, &b, 1); acc = 0; nacc = 0; }
                in_group = 0;
                bits++;
            }
        }
        cur = c;
    }
    if (nacc) { uint8_t b = (uint8_t)acc; st_bytes_append(out, &b, 1); }
    (void)group_bytes;
}

static void sit_entry(StBytes *a, int rm, int dm, const StMacFile *f, const StBytes *rc, const StBytes *dc) {
    uint8_t h[112];
    memset(h, 0, sizeof h);
    h[0] = (uint8_t)rm;
    h[1] = (uint8_t)dm;
    uint8_t nm[64];
    int nl = st_utf8_to_macroman(f->name, nm, 63);
    h[2] = (uint8_t)nl;
    memcpy(h + 3, nm, (size_t)nl);
    memcpy(h + 66, f->finfo, 10);
    be32(h + 76, f->ctime);
    be32(h + 80, f->mtime);
    be32(h + 84, (uint32_t)f->rsrc.n);
    be32(h + 88, (uint32_t)f->data.n);
    be32(h + 92, rc ? (uint32_t)rc->n : 0);
    be32(h + 96, dc ? (uint32_t)dc->n : 0);
    be16(h + 100, st_crc16_arc(0, f->rsrc.p, f->rsrc.n));
    be16(h + 102, st_crc16_arc(0, f->data.p, f->data.n));
    be16(h + 110, st_crc16_arc(0, h, 110));
    st_bytes_append(a, h, 112);
    if (rc) st_bytes_append(a, rc->p, rc->n);
    if (dc) st_bytes_append(a, dc->p, dc->n);
}

static void test_sit_classic(void) {
    StBytes ar = {0}, rc = {0}, dc = {0};
    uint8_t hdr[22] = {'S', 'I', 'T', '!', 0, 3, 0, 0, 0, 0, 'r', 'L', 'a', 'u', 1};
    st_bytes_append(&ar, hdr, 22);
    StMacFile dir, a, b, c, out;
    st_macfile_init(&dir);
    snprintf(dir.name, sizeof dir.name, "Stuff Folder");
    sit_entry(&ar, 32, 32, &dir, NULL, NULL);
    st_make_file(&a, "rle", 4000, 300, 21);
    enc_rle90(&a.rsrc, &rc);
    enc_rle90(&a.data, &dc);
    sit_entry(&ar, 1, 1, &a, &rc, &dc);
    st_make_file(&b, "huff", 5000, 0, 22);
    enc_huffman(&b.data, &dc);
    sit_entry(&ar, 0, 3, &b, NULL, &dc);
    sit_entry(&ar, 33, 33, &dir, NULL, NULL);
    st_make_file(&c, "lzw", 20000, 700, 23);
    enc_lzw(&c.data, &dc);
    enc_lzw(&c.rsrc, &rc);
    sit_entry(&ar, 2, 2, &c, &rc, &dc);
    CHECK(st_sit_detect(ar.p, ar.n) == 1);
    StArchive x = {0};
    StErr err;
    CHECK_RC(st_sit_decode(ar.p, ar.n, &x, NULL, &err), ST_OK);
    CHECK(x.n == 4);
    CHECK(find(&x, "Stuff Folder") && find(&x, "Stuff Folder")->is_dir);
    CHECK(find(&x, "Stuff Folder/rle") && st_same_file(&a, &find(&x, "Stuff Folder/rle")->file));
    CHECK(find(&x, "Stuff Folder/huff") && st_same_file(&b, &find(&x, "Stuff Folder/huff")->file));
    CHECK(find(&x, "lzw") && st_same_file(&c, &find(&x, "lzw")->file));
    st_archive_free(&x);
    /* Corrupt a header: the header CRC catches it. */
    ar.p[22 + 112 + 5] ^= 0x40;
    CHECK_RC(st_sit_decode(ar.p, ar.n, &x, NULL, &err), ST_ECHECKSUM);
    ar.p[22 + 112 + 5] ^= 0x40;
    /* An unsupported method is named, not guessed at. */
    StBytes u = {0};
    st_bytes_append(&u, hdr, 22);
    sit_entry(&u, 0, 13, &b, NULL, &dc);
    CHECK_RC(st_sit_decode(u.p, u.n, &x, NULL, &err), ST_EUNSUPPORTED);
    CHECK(strstr(err.msg, "method 13") != NULL);
    /* Folder end without start. */
    u.n = 22;
    sit_entry(&u, 33, 33, &dir, NULL, NULL);
    CHECK_RC(st_sit_decode(u.p, u.n, &x, NULL, &err), ST_EFORMAT);
    st_bytes_free(&u);
    st_bytes_free(&ar); st_bytes_free(&rc); st_bytes_free(&dc);
    st_macfile_free(&a); st_macfile_free(&b); st_macfile_free(&c);
    (void)out;
}

/* StuffIt 5: header, one folder, one stored file inside it. */
static void sit5_entry(StBytes *a, uint32_t parent, int folder, const StMacFile *f, uint32_t first_child) {
    uint8_t h[48 + 64];
    memset(h, 0, sizeof h);
    size_t nl = strlen(f->name);
    be32(h, 0xA5A5A5A5u);
    h[4] = 1;
    be16(h + 6, (uint16_t)(48 + nl));
    h[9] = folder ? 0x40 : 0x10;
    be32(h + 10, f->ctime);
    be32(h + 14, f->mtime);
    be32(h + 26, parent);
    be16(h + 30, (uint16_t)nl);
    be32(h + 34, folder ? first_child : (uint32_t)f->data.n);
    be32(h + 38, folder ? 0 : (uint32_t)f->data.n);
    be16(h + 42, st_crc16_arc(0, f->data.p, f->data.n));
    memcpy(h + 48, f->name, nl);
    st_bytes_append(a, h, 48 + nl);
    uint8_t second[36];
    memset(second, 0, sizeof second);
    be16(second, f->rsrc.n ? 1 : 0);
    memcpy(second + 4, f->finfo, 10);
    st_bytes_append(a, second, 36);
    if (!folder && f->rsrc.n) {
        uint8_t r[14];
        memset(r, 0, sizeof r);
        be32(r, (uint32_t)f->rsrc.n);
        be32(r + 4, (uint32_t)f->rsrc.n);
        be16(r + 8, st_crc16_arc(0, f->rsrc.p, f->rsrc.n));
        st_bytes_append(a, r, 14);
    }
    if (!folder) { st_bytes_append(a, f->rsrc.p, f->rsrc.n); st_bytes_append(a, f->data.p, f->data.n); }
}

static void test_sit5(void) {
    StBytes ar = {0};
    uint8_t h[114];
    memset(h, 0, sizeof h);
    memcpy(h, "StuffIt (c)1997-1998 Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n", 80);
    h[80] = 0x1A; h[82] = 5; h[83] = 0x10;
    be32(h + 88, 114);
    be16(h + 92, 1);
    st_bytes_append(&ar, h, sizeof h);
    StMacFile dir, f;
    st_macfile_init(&dir);
    snprintf(dir.name, sizeof dir.name, "Folder");
    sit5_entry(&ar, 0, 1, &dir, 114 + 48 + 6 + 36);
    st_make_file(&f, "Doc", 999, 123, 31);
    sit5_entry(&ar, 114, 0, &f, 0);
    CHECK(st_sit_detect(ar.p, ar.n) == 5);
    StArchive x = {0};
    StErr err;
    CHECK_RC(st_sit_decode(ar.p, ar.n, &x, NULL, &err), ST_OK);
    CHECK(x.n == 2 && find(&x, "Folder/Doc") && st_same_file(&f, &find(&x, "Folder/Doc")->file));
    st_archive_free(&x);
    ar.p[ar.n - 1] ^= 1; /* data CRC */
    CHECK_RC(st_sit_decode(ar.p, ar.n, &x, NULL, &err), ST_ECHECKSUM);
    st_bytes_free(&ar);
    st_macfile_free(&f);
}

/* ---- Compact Pro: RLE (0x81 escape) and an LZH stream with fixed-length
 * complete codes, long enough to cross a 0x1FFF0 block boundary. ---- */
/* Callers keep 0x81 out of the input, so only runs need escaping. */
static void enc_rle81(const StBytes *in, StBytes *out) {
    out->n = 0;
    for (size_t i = 0; i < in->n;) {
        uint8_t b = in->p[i];
        size_t run = 1;
        while (i + run < in->n && in->p[i + run] == b && run < 255) run++;
        if (run > 3) {
            uint8_t e[4] = {b, 0x81, 0x82, (uint8_t)run};
            st_bytes_append(out, e, 4);
            i += run;
        } else {
            st_bytes_append(out, &b, 1);
            i++;
        }
    }
}

static void lzh_table(BitW *w, int nsym, int len) {
    bw_msb(w, (uint32_t)(nsym / 2), 8);
    for (int i = 0; i < nsym / 2; i++) bw_msb(w, (uint32_t)(len << 4 | len), 8);
}

/* Literals plus a few matches; codes have fixed lengths 8/6/7. */
static void enc_cpt_lzh(const StBytes *in, StBytes *out) {
    out->n = 0;
    BitW w = {out, 0, 0};
    size_t i = 0;
    while (i < in->n) {
        lzh_table(&w, 256, 8);
        lzh_table(&w, 64, 6);
        lzh_table(&w, 128, 7);
        size_t codes = out->n * 8 + (size_t)w.bits;
        uint32_t work = 0;
        while (work < 0x1FFF0 && i < in->n) {
            if (i >= 64 && i + 10 < in->n && !memcmp(in->p + i, in->p + i - 64, 10)) {
                bw_msb(&w, 0, 1);
                bw_msb(&w, 10, 6);           /* length 10 */
                bw_msb(&w, 1, 7);            /* distance 64 = 1 << 6 | 0 */
                bw_msb(&w, 0, 6);
                i += 10;
                work += 3;
            } else {
                bw_msb(&w, 1, 1);
                bw_msb(&w, in->p[i++], 8);
                work += 2;
            }
        }
        size_t used = out->n * 8 + (size_t)w.bits - codes;
        size_t pad = ((used + 15) & ~(size_t)15) + 16 - used;
        while (pad--) bw_msb(&w, 0, 1);
    }
    bw_flush(&w);
}

static void test_cpt(void) {
    StMacFile a, b;
    st_make_file(&a, "Small", 3000, 200, 71);
    st_make_file(&b, "Large", 150000, 0, 72);
    for (size_t k = 0; k < b.data.n; k++) if (b.data.p[k] == 0x81) b.data.p[k] = 'q';
    for (size_t k = 0; k < a.data.n; k++) if (a.data.p[k] == 0x81) a.data.p[k] = 'q';
    for (size_t k = 0; k < a.rsrc.n; k++) if (a.rsrc.p[k] == 0x81) a.rsrc.p[k] = 'q';
    StBytes ar = {0}, r1 = {0}, d1 = {0}, d2 = {0}, rl = {0};
    enc_rle81(&a.rsrc, &r1);
    enc_rle81(&a.data, &d1);
    enc_rle81(&b.data, &rl);
    enc_cpt_lzh(&rl, &d2);
    uint8_t h[8] = {1, 1, 0x12, 0x34};
    st_bytes_append(&ar, h, 8);
    uint32_t off1 = (uint32_t)ar.n;
    st_bytes_append(&ar, r1.p, r1.n);
    st_bytes_append(&ar, d1.p, d1.n);
    uint32_t off2 = (uint32_t)ar.n;
    st_bytes_append(&ar, d2.p, d2.n);
    uint32_t dir = (uint32_t)ar.n;
    be32(ar.p + 4, dir);
    uint8_t dh[7] = {0, 0, 0, 0, 0, 3, 0};
    st_bytes_append(&ar, dh, 7);
    uint8_t fold[8] = {0x80 | 6, 'F', 'o', 'l', 'd', 'e', 'r', 0};
    st_bytes_append(&ar, fold, 7);
    uint8_t cnt[2] = {0, 1};
    st_bytes_append(&ar, cnt, 2);
    const StMacFile *fs[2] = {&a, &b};
    uint32_t offs[2] = {off1, off2};
    size_t rcl[2] = {r1.n, 0}, dcl[2] = {d1.n, d2.n};
    for (int k = 0; k < 2; k++) {
        uint8_t e[64];
        size_t nl = strlen(fs[k]->name);
        e[0] = (uint8_t)nl;
        memcpy(e + 1, fs[k]->name, nl);
        uint8_t *q = e + 1 + nl;
        q[0] = 1;
        be32(q + 1, offs[k]);
        memcpy(q + 5, fs[k]->finfo, 8);
        be32(q + 13, fs[k]->ctime);
        be32(q + 17, fs[k]->mtime);
        memcpy(q + 21, fs[k]->finfo + 8, 2);
        be32(q + 23, ~st_crc32(st_crc32(0, fs[k]->rsrc.p, fs[k]->rsrc.n), fs[k]->data.p, fs[k]->data.n));
        be16(q + 27, k ? 4 : 0);
        be32(q + 29, (uint32_t)fs[k]->rsrc.n);
        be32(q + 33, (uint32_t)fs[k]->data.n);
        be32(q + 37, (uint32_t)rcl[k]);
        be32(q + 41, (uint32_t)dcl[k]);
        st_bytes_append(&ar, e, 1 + nl + 45);
    }
    CHECK(st_cpt_detect(ar.p, ar.n));
    StArchive x = {0};
    StErr err;
    CHECK_RC(st_cpt_decode(ar.p, ar.n, &x, NULL, &err), ST_OK);
    CHECK(x.n == 3 && find(&x, "Folder") && find(&x, "Folder")->is_dir);
    CHECK(find(&x, "Folder/Small") && st_same_file(&a, &find(&x, "Folder/Small")->file));
    CHECK(find(&x, "Large") && st_same_file(&b, &find(&x, "Large")->file));
    st_archive_free(&x);
    ar.p[off2 + 5000] ^= 0x10; /* LZH damage: CRC or format error */
    int rc = st_cpt_decode(ar.p, ar.n, &x, NULL, &err);
    CHECK(rc == ST_ECHECKSUM || rc == ST_EFORMAT);
    st_bytes_free(&ar); st_bytes_free(&r1); st_bytes_free(&d1); st_bytes_free(&d2); st_bytes_free(&rl);
    st_macfile_free(&a); st_macfile_free(&b);
}

int main(void) {
    test_zip();
    test_sit_classic();
    test_sit5();
    test_cpt();
    return st_test_done("test_storage_archives");
}
