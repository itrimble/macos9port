/* StuffIt fork decompressors we implement: 0 store, 1 RLE90, 2 LZW
 * (compress-style, 14-bit), 3 static Huffman with an in-stream tree. */
#include "st_sit.h"

#include <stdlib.h>

static int put(StBytes *o, uint8_t b, size_t want) {
    if (o->n >= want) return 1; /* stop: output complete */
    o->p[o->n++] = b;
    return 0;
}

static int rle90(const uint8_t *p, size_t n, StBytes *o, size_t want) {
    uint8_t last = 0;
    int have = 0;
    for (size_t i = 0; i < n && o->n < want; i++) {
        if (p[i] != 0x90) { last = p[i]; have = 1; put(o, last, want); continue; }
        if (++i >= n) return -1;
        if (p[i] == 0) { last = 0x90; have = 1; put(o, 0x90, want); continue; }
        if (!have) return -1;
        for (int k = 1; k < p[i]; k++) put(o, last, want);
    }
    return 0;
}

/* Unix compress semantics: codes start at 9 bits, grow to 14, code 256
 * clears. Codes are read in groups of eight; when the width changes the
 * rest of the current group is skipped. */
static int lzw(const uint8_t *p, size_t n, StBytes *o, size_t want) {
    enum { MAXBITS = 14, MAXCODE = 1 << MAXBITS };
    uint16_t *prefix = malloc(MAXCODE * sizeof *prefix);
    uint8_t *suffix = malloc(MAXCODE), *stack = malloc(MAXCODE);
    if (!prefix || !suffix || !stack) { free(prefix); free(suffix); free(stack); return -2; }
    for (int i = 0; i < 256; i++) suffix[i] = (uint8_t)i;
    StBits b;
    st_bits_init(&b, p, n);
    int bits = 9, next = 257, old = -1, rc = 0;
    uint8_t first = 0;
    size_t group_start = 0, in_group = 0;
    while (o->n < want) {
        if (b.pos + (size_t)bits > (uint64_t)n * 8) break;
        int code = (int)st_bits_lsb(&b, bits);
        in_group++;
        if (code == 256) {
            /* skip to the end of this group of eight codes */
            b.pos = group_start + (size_t)bits * 8 * ((in_group + 7) / 8);
            group_start = b.pos;
            in_group = 0;
            bits = 9;
            next = 257;
            old = -1;
            continue;
        }
        int sp = 0, cur = code;
        if (old < 0) {
            if (code > 255) { rc = -1; break; }
            first = (uint8_t)code;
            put(o, first, want);
            old = code;
            continue;
        }
        if (code > next) { rc = -1; break; }
        if (code == next) { stack[sp++] = first; cur = old; }
        while (cur > 255) { stack[sp++] = suffix[cur]; cur = prefix[cur]; }
        first = (uint8_t)cur;
        stack[sp++] = first;
        while (sp) put(o, stack[--sp], want);
        if (next < MAXCODE) { prefix[next] = (uint16_t)old; suffix[next] = first; next++; }
        old = code;
        if (next >= (1 << bits) && bits < MAXBITS) {
            b.pos = group_start + (size_t)bits * 8 * ((in_group + 7) / 8);
            group_start = b.pos;
            in_group = 0;
            bits++;
        }
    }
    free(prefix); free(suffix); free(stack);
    return rc;
}

/* Tree: 1 bit = leaf followed by an 8-bit byte; 0 bit = internal node. */
typedef struct { int16_t kid[2]; int16_t leaf; } HNode;

static int read_tree(StBits *b, HNode *t, int *count, int depth) {
    if (*count >= 511 || depth > 256 || st_bits_over(b)) return -1;
    int me = (*count)++;
    if (st_bits_msb(b, 1)) {
        t[me].leaf = (int16_t)st_bits_msb(b, 8);
        return me;
    }
    t[me].leaf = -1;
    int l = read_tree(b, t, count, depth + 1);
    if (l < 0) return -1;
    int r = read_tree(b, t, count, depth + 1);
    if (r < 0) return -1;
    t[me].kid[0] = (int16_t)l;
    t[me].kid[1] = (int16_t)r;
    return me;
}

static int huffman(const uint8_t *p, size_t n, StBytes *o, size_t want) {
    HNode t[512];
    int count = 0;
    StBits b;
    st_bits_init(&b, p, n);
    if (read_tree(&b, t, &count, 0) != 0) return -1;
    if (t[0].leaf >= 0) { while (o->n < want) put(o, (uint8_t)t[0].leaf, want); return 0; }
    while (o->n < want) {
        int node = 0;
        while (t[node].leaf < 0) {
            if (st_bits_over(&b)) return -1;
            node = t[node].kid[st_bits_msb(&b, 1)];
        }
        if (st_bits_over(&b)) return -1;
        put(o, (uint8_t)t[node].leaf, want);
    }
    return 0;
}

const char *st_sit_method_name(int m) {
    switch (m & 0x0F) {
    case 0: return "none";
    case 1: return "RLE90";
    case 2: return "LZW";
    case 3: return "Huffman";
    case 5: return "LZAH";
    case 6: return "fixed Huffman";
    case 8: return "MW";
    case 13: return "LZ+Huffman (method 13)";
    case 14: return "installer";
    case 15: return "Arsenic";
    default: return "unknown";
    }
}

int st_sit_unpack(int method, const uint8_t *p, size_t n, size_t want, uint16_t crc, int crc_kind,
                  StBytes *out, const StLimits *lim, const char *what, StErr *err) {
    if (method & 0x10) return st_fail(err, ST_EUNSUPPORTED, "%s is encrypted", what);
    if (lim && want > lim->max_entry) return st_fail(err, ST_ELIMIT, "%s is too large", what);
    out->n = 0;
    if (st_bytes_reserve(out, want + 1)) return st_fail(err, ST_ENOMEM, "out of memory");
    int rc;
    switch (method & 0x0F) {
    case 0:
        if (n < want) return st_fail(err, ST_EFORMAT, "%s is truncated", what);
        memcpy(out->p, p, want);
        out->n = want;
        rc = 0;
        break;
    case 1: rc = rle90(p, n, out, want); break;
    case 2: rc = lzw(p, n, out, want); break;
    case 3: rc = huffman(p, n, out, want); break;
    default:
        return st_fail(err, ST_EUNSUPPORTED, "%s uses StuffIt method %d (%s)", what, method & 0x0F,
                       st_sit_method_name(method));
    }
    if (rc == -2) return st_fail(err, ST_ENOMEM, "out of memory");
    if (rc || out->n != want)
        return st_fail(err, ST_EFORMAT, "%s: %s stream is corrupt (%zu of %zu bytes)", what,
                       st_sit_method_name(method), out->n, want);
    if (crc_kind) {
        uint16_t got = st_crc16_arc(0, out->p, out->n);
        if (got != crc) return st_fail(err, ST_ECHECKSUM, "%s CRC %04x, expected %04x", what, got, crc);
    }
    return ST_OK;
}
