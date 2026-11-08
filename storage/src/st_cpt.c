/* Compact Pro (.cpt): a directory at the end of the archive; each fork is
 * RLE-coded (0x81 escape), optionally after an LZH stage whose blocks
 * carry three nibble-packed Huffman tables (literal 256, length 64,
 * offset 128) and use an 8K ring window. Layout and coding were derived
 * from archive bytes compared with their stored (uncompressed) copies. */
#include "st_int.h"

#include <stdio.h>
#include <stdlib.h>

int st_cpt_detect(const uint8_t *p, size_t n) {
    if (n < 16 || p[0] != 0x01 || p[1] != 0x01) return 0;
    uint32_t d = st_be32(p + 4);
    if (d < 8 || !st_span(n, d, 7)) return 0;
    uint16_t entries = st_be16(p + d + 4);
    return entries > 0 && st_span(n, (uint64_t)d + 7, p[d + 6]);
}

static int rle81(const uint8_t *p, size_t n, StBytes *o, size_t want) {
    uint8_t last = 0;
    for (size_t i = 0; i < n && o->n < want;) {
        uint8_t x = p[i++];
        if (x != 0x81) { o->p[o->n++] = last = x; continue; }
        if (i >= n) return -1;
        uint8_t y = p[i++];
        if (y == 0x82) {
            if (i >= n) return -1;
            uint8_t cnt = p[i++];
            if (cnt == 0) { /* literal 0x81 0x82 */
                o->p[o->n++] = 0x81;
                if (o->n < want) o->p[o->n++] = last = 0x82;
                continue;
            }
            for (int k = 1; k < cnt && o->n < want; k++) o->p[o->n++] = last;
        } else {
            o->p[o->n++] = 0x81;
            if (o->n < want) o->p[o->n++] = last = y;
        }
    }
    return o->n == want ? 0 : -1;
}

static int read_table(StBits *b, StHuff *h, int nsym) {
    uint8_t lens[256];
    memset(lens, 0, sizeof lens);
    int count = (int)st_bits_msb(b, 8);
    for (int i = 0; i < count; i++) {
        uint32_t v = st_bits_msb(b, 8);
        if (2 * i < nsym) lens[2 * i] = (uint8_t)(v >> 4);
        if (2 * i + 1 < nsym) lens[2 * i + 1] = (uint8_t)(v & 15);
    }
    return st_bits_over(b) || st_huff_build(h, lens, nsym) ? -1 : 0;
}

/* LZH stage: tables are re-read every 0x1FFF0 units of work, where a
 * literal costs 2 and a match 3. Block size, weights and the boundary
 * rule were found by matching every block of the Aaru corpus. */
static int lzh(const uint8_t *p, size_t n, StBytes *o, size_t want) {
    StBits b;
    st_bits_init(&b, p, n);
    StHuff *t = malloc(3 * sizeof *t);
    if (!t) return -2;
    int rc = 0;
    while (o->n < want && rc == 0) {
        if (read_table(&b, &t[0], 256) || read_table(&b, &t[1], 64) || read_table(&b, &t[2], 128)) { rc = -1; break; }
        size_t codes = b.pos; /* byte aligned: tables are whole bytes */
        uint32_t work = 0;
        while (work < 0x1FFF0 && o->n < want) {
            if (st_bits_msb(&b, 1)) {
                int s = st_huff_read(&t[0], &b, 0);
                if (s < 0) { rc = -1; break; }
                o->p[o->n++] = (uint8_t)s;
                work += 2;
            } else {
                int len = st_huff_read(&t[1], &b, 0), hi = st_huff_read(&t[2], &b, 0);
                if (len < 0 || hi < 0) { rc = -1; break; }
                size_t dist = ((size_t)hi << 6) | st_bits_msb(&b, 6);
                if (!dist) dist = 8192;
                /* The 8K ring starts zero-filled: reaching before the start reads zeros. */
                for (int k = 0; k < len && o->n < want; k++, o->n++)
                    o->p[o->n] = dist <= o->n ? o->p[o->n - dist] : 0;
                work += 3;
            }
            if (st_bits_over(&b)) { rc = -1; break; }
        }
        /* Codes are fetched in 16-bit words with one word of lookahead:
         * the next block starts one word past the last word touched. */
        b.pos = codes + ((b.pos - codes + 15) & ~(size_t)15) + 16;
    }
    free(t);
    return rc;
}

static int fork(const uint8_t *p, size_t n, int use_lzh, size_t want, StBytes *out, const StLimits *lim,
                const char *what, StErr *err) {
    if (lim && want > lim->max_entry) return st_fail(err, ST_ELIMIT, "%s is too large", what);
    out->n = 0;
    if (!want) return ST_OK;
    if (st_bytes_reserve(out, want + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    if (!use_lzh) {
        if (rle81(p, n, out, want)) return st_fail(err, ST_EFORMAT, "%s: RLE stream is corrupt", what);
        return ST_OK;
    }
    /* LZH output is an RLE stream of unknown length; grow until RLE fills want. */
    StBytes mid = {0};
    size_t cap = want + want / 2 + 1024;
    int rc;
    for (;;) {
        mid.n = 0;
        if (st_bytes_reserve(&mid, cap)) { st_bytes_free(&mid); return st_fail(err, ST_ENOMEM, "out of memory"); }
        rc = lzh(p, n, &mid, cap);
        out->n = 0;
        if (rc == -2) { st_bytes_free(&mid); return st_fail(err, ST_ENOMEM, "out of memory"); }
        if (rle81(mid.p, mid.n, out, want) == 0) { rc = 0; break; }
        if (mid.n < cap || cap > (size_t)want * 4 + 65536) { rc = -1; break; }
        cap *= 2;
    }
    st_bytes_free(&mid);
    if (rc) return st_fail(err, ST_EFORMAT, "%s: LZH stream is corrupt", what);
    return ST_OK;
}

int st_cpt_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err) {
    if (!st_cpt_detect(p, n)) return st_fail(err, ST_EFORMAT, "not a Compact Pro archive");
    uint32_t d = st_be32(p + 4);
    uint16_t total = st_be16(p + d + 4);
    size_t q = (size_t)d + 7 + p[d + 6];
    char dir[4096] = "";
    int left[64], depth = 0, rc = ST_OK;
    for (uint32_t e = 0; e < total && rc == ST_OK; e++) {
        while (depth > 0 && left[depth - 1] == 0) {
            depth--;
            char *s = strrchr(dir, '/');
            if (s) *s = 0; else dir[0] = 0;
        }
        for (int k = 0; k < depth; k++) left[k]--; /* counts are descendants */
        if (!st_span(n, q, 1)) { rc = st_fail(err, ST_EFORMAT, "Compact Pro directory is truncated"); break; }
        uint8_t nl = p[q] & 0x7F;
        int folder = p[q] & 0x80;
        StMacFile f;
        st_macfile_init(&f);
        if (!st_span(n, q + 1, (uint64_t)nl + (folder ? 2 : 45))) { rc = st_fail(err, ST_EFORMAT, "Compact Pro entry is truncated"); break; }
        if ((rc = st_name_from_macroman(&f, p + q + 1, nl, err))) break;
        char comp[256], path[4096];
        snprintf(comp, sizeof comp, "%s", f.name);
        for (char *c = comp; *c; c++) if (*c == '/') *c = ':';
        if ((size_t)snprintf(path, sizeof path, "%s%s%s", dir, *dir ? "/" : "", comp) >= sizeof path) { rc = st_fail(err, ST_ELIMIT, "path too long"); break; }
        const uint8_t *h = p + q + 1 + nl;
        if (folder) {
            if (depth == 64) { rc = st_fail(err, ST_ELIMIT, "folders nest too deeply"); break; }
            left[depth++] = st_be16(h);
            rc = st_archive_add(out, path, 1, &f, lim, err);
            snprintf(dir, sizeof dir, "%s", path);
            q += 1 + nl + 2;
            continue;
        }
        uint32_t off = st_be32(h + 1);
        memcpy(f.finfo, h + 5, 8);
        f.ctime = st_be32(h + 13);
        f.mtime = st_be32(h + 17);
        memcpy(f.finfo + 8, h + 21, 2);
        uint32_t crc = st_be32(h + 23);
        uint16_t flags = st_be16(h + 27);
        uint32_t ru = st_be32(h + 29), du = st_be32(h + 33), rcl = st_be32(h + 37), dcl = st_be32(h + 41);
        q += 1 + nl + 45;
        if (flags & 1) { rc = st_fail(err, ST_EUNSUPPORTED, "Compact Pro entry %s is encrypted", path); break; }
        if (!st_span(n, off, (uint64_t)rcl + dcl)) { rc = st_fail(err, ST_EFORMAT, "Compact Pro forks of %s are truncated", path); break; }
        char what[300];
        snprintf(what, sizeof what, "resource fork of %.200s", f.name);
        rc = fork(p + off, rcl, flags & 2, ru, &f.rsrc, lim, what, err);
        snprintf(what, sizeof what, "data fork of %.200s", f.name);
        if (rc == ST_OK) rc = fork(p + off + rcl, dcl, flags & 4, du, &f.data, lim, what, err);
        if (rc == ST_OK) {
            uint32_t got = ~st_crc32(st_crc32(0, f.rsrc.p, f.rsrc.n), f.data.p, f.data.n);
            if (got != crc) rc = st_fail(err, ST_ECHECKSUM, "Compact Pro CRC mismatch for %s", path);
        }
        if (rc == ST_OK) rc = st_archive_add(out, path, 0, &f, lim, err);
        if (rc) st_macfile_free(&f);
    }
    if (rc) st_archive_free(out);
    return rc;
}
