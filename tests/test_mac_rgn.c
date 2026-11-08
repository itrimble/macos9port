/* The region engine against an independent oracle: pixel sets, brute-force operations, and a separate, simple row-by-row encoder. */
#include "mac_rgn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);fails++;}}while(0)
enum { LO = -4, HI = 24, N = HI - LO };            /* the grid: x and y from -4 to 23 */
typedef struct { uint8_t p[N][N]; } Pix;           /* p[y - LO][x - LO] */
static unsigned long seed = 12345;
static unsigned rnd(void) { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return (unsigned)(seed >> 33); }
static void fill_rect(Pix *s, int t, int l, int b, int r) { for (int y = t; y < b; y++) for (int x = l; x < r; x++) s->p[y - LO][x - LO] = 1; }
static void random_pix(Pix *s) {
    memset(s, 0, sizeof *s);
    int k = (int)(rnd() % 5);
    for (int i = 0; i < k; i++) {
        int t = LO + 1 + (int)(rnd() % (N - 4)), l = LO + 1 + (int)(rnd() % (N - 4)), b = t + 1 + (int)(rnd() % 9), r = l + 1 + (int)(rnd() % 9);
        if (b > HI - 1) b = HI - 1; if (r > HI - 1) r = HI - 1;
        fill_rect(s, t, l, b, r);
    }
}
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)((unsigned)v >> 8); p[1] = (uint8_t)v; }
/* The oracle's encoder: bounding box, the row-by-row difference of edge sets, a 10-byte header for a rectangle or an empty region. */
static size_t oracle_encode(const Pix *s, uint8_t *out) {
    int t = 999, l = 999, b = -999, r = -999, count = 0;
    for (int y = LO; y < HI; y++) for (int x = LO; x < HI; x++) if (s->p[y - LO][x - LO]) { count++; if (y < t) t = y; if (y + 1 > b) b = y + 1; if (x < l) l = x; if (x + 1 > r) r = x + 1; }
    if (!count) { put16(out, 10); memset(out + 2, 0, 8); return 10; }
    if (count == (b - t) * (r - l)) { put16(out, 10); put16(out + 2, t); put16(out + 4, l); put16(out + 6, b); put16(out + 8, r); return 10; }
    size_t at = 10; int prev[N + 2], np = 0;
    for (int y = t; y <= b; y++) {
        int cur[N + 2], nc = 0;
        for (int x = LO; x <= HI; x++) { int in = y < b && x < HI && s->p[y - LO][x - LO], left = x > LO && y < b && s->p[y - LO][x - 1 - LO]; if (in != left) cur[nc++] = x; }
        int diff[2 * N + 4], nd = 0, a = 0, c = 0;                     /* symmetric difference of the previous row's edges and this row's */
        while (a < np || c < nc) { if (c >= nc || (a < np && prev[a] < cur[c])) diff[nd++] = prev[a++]; else if (a >= np || cur[c] < prev[a]) diff[nd++] = cur[c++]; else { a++; c++; } }
        if (nd) { put16(out + at, y); at += 2; for (int i = 0; i < nd; i++) { put16(out + at, diff[i]); at += 2; } put16(out + at, 0x7fff); at += 2; }
        memcpy(prev, cur, (size_t)nc * sizeof *cur); np = nc;
    }
    put16(out + at, 0x7fff); at += 2;
    put16(out, (int)at); put16(out + 2, t); put16(out + 4, l); put16(out + 6, b); put16(out + 8, r);
    return at;
}
static int region_from(const Pix *s, MacRgn *g) {
    uint8_t buf[4096]; size_t n = oracle_encode(s, buf); const char *why;
    return mac_rgn_decode(buf, n, g, &why);
}
static int same_pixels(const MacRgn *g, const Pix *s) {
    for (int y = LO; y < HI; y++) for (int x = LO; x < HI; x++) if (mac_rgn_contains(g, x, y) != s->p[y - LO][x - LO]) return 0;
    return 1;
}
static int encodes_like(const MacRgn *g, const Pix *s) {
    uint8_t a[4096], b[4096]; size_t nb = oracle_encode(s, b), na = mac_rgn_encode(g, a, sizeof a);
    return na == nb && na == mac_rgn_encoded_size(g) && !memcmp(a, b, nb);
}
static void oracle_checks(void) {
    for (int iter = 0; iter < 3000; iter++) {
        Pix pa, pb, pr; random_pix(&pa); random_pix(&pb);
        MacRgn a, b, r; mac_rgn_init(&a); mac_rgn_init(&b); mac_rgn_init(&r); const char *why;
        CHECK(region_from(&pa, &a) == 0 && region_from(&pb, &b) == 0);
        CHECK(same_pixels(&a, &pa) && encodes_like(&a, &pa));                         /* decode then encode reproduces the oracle's bytes */
        for (int op = 0; op < 4; op++) {
            for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) {
                int u = pa.p[y][x], v = pb.p[y][x];
                pr.p[y][x] = (uint8_t)(op == MAC_RGN_OP_SECT ? (u && v) : op == MAC_RGN_OP_UNION ? (u || v) : op == MAC_RGN_OP_DIFF ? (u && !v) : (u != v));
            }
            CHECK(mac_rgn_op(op, &a, &b, &r, &why) == 0);
            if (!same_pixels(&r, &pr) || !encodes_like(&r, &pr)) { CHECK(0); fprintf(stderr, "  iteration %d op %d\\n", iter, op); }
        }
        /* queries */
        MacRect bb; mac_rgn_bbox(&a, &bb); int t = 999, l = 999, bt = -999, rt = -999, cnt = 0;
        for (int y = 0; y < N; y++) for (int x = 0; x < N; x++) if (pa.p[y][x]) { cnt++; if (y + LO < t) t = y + LO; if (y + LO + 1 > bt) bt = y + LO + 1; if (x + LO < l) l = x + LO; if (x + LO + 1 > rt) rt = x + LO + 1; }
        CHECK(mac_rgn_is_empty(&a) == (cnt == 0));
        if (cnt) CHECK(bb.t == t && bb.l == l && bb.b == bt && bb.r == rt); else CHECK(bb.t == 0 && bb.l == 0 && bb.b == 0 && bb.r == 0);
        MacRect q = {LO + (int)(rnd() % N), LO + (int)(rnd() % N), 0, 0}; q.b = q.t + (int)(rnd() % 6) - 1; q.r = q.l + (int)(rnd() % 6) - 1;
        int any = 0; for (int y = q.t; y < q.b; y++) for (int x = q.l; x < q.r; x++) if (y >= LO && y < HI && x >= LO && x < HI && pa.p[y - LO][x - LO]) any = 1;
        CHECK(mac_rgn_rect_in(&a, q) == any);
        CHECK(mac_rgn_equal(&a, &a) && mac_rgn_equal(&a, &b) == !memcmp(&pa, &pb, sizeof pa));
        /* offset: every pixel moves */
        MacRgn o; mac_rgn_init(&o); mac_rgn_copy(&o, &a); int dh = (int)(rnd() % 5) - 2, dv = (int)(rnd() % 5) - 2; mac_rgn_offset(&o, dh, dv);
        for (int y = LO + 3; y < HI - 3; y++) for (int x = LO + 3; x < HI - 3; x++) CHECK(mac_rgn_contains(&o, x + dh, y + dv) == pa.p[y - LO][x - LO]);
        /* a region op's result is independent of operand order for the symmetric operations, and idempotent where it should be */
        MacRgn r2; mac_rgn_init(&r2); mac_rgn_op(MAC_RGN_OP_UNION, &a, &b, &r, &why); mac_rgn_op(MAC_RGN_OP_UNION, &b, &a, &r2, &why); CHECK(mac_rgn_equal(&r, &r2));
        mac_rgn_op(MAC_RGN_OP_SECT, &a, &b, &r, &why); mac_rgn_op(MAC_RGN_OP_SECT, &b, &a, &r2, &why); CHECK(mac_rgn_equal(&r, &r2));
        mac_rgn_op(MAC_RGN_OP_UNION, &a, &a, &r, &why); CHECK(mac_rgn_equal(&r, &a));
        mac_rgn_op(MAC_RGN_OP_XOR, &a, &a, &r, &why); CHECK(mac_rgn_is_empty(&r));
        mac_rgn_free(&a); mac_rgn_free(&b); mac_rgn_free(&r); mac_rgn_free(&r2); mac_rgn_free(&o);
    }
}
static void byte_checks(void) {
    const char *why; MacRgn g; mac_rgn_init(&g);
    /* An L: rows 0-1 span x 0-3, rows 2-3 span x 0-1. */
    static const uint8_t L[] = {0x00,0x24, 0,0, 0,0, 0,4, 0,4,  0,0, 0,0, 0,4, 0x7f,0xff,  0,2, 0,2, 0,4, 0x7f,0xff,  0,4, 0,0, 0,2, 0x7f,0xff,  0x7f,0xff};
    CHECK(mac_rgn_decode(L, sizeof L, &g, &why) == 0 && g.n == 2);
    CHECK(g.r[0].t == 0 && g.r[0].l == 0 && g.r[0].b == 2 && g.r[0].r == 4 && g.r[1].t == 2 && g.r[1].l == 0 && g.r[1].b == 4 && g.r[1].r == 2);
    uint8_t out[128]; CHECK(mac_rgn_encoded_size(&g) == sizeof L && mac_rgn_encode(&g, out, sizeof out) == sizeof L && !memcmp(out, L, sizeof L));
    CHECK(mac_rgn_encode(&g, out, sizeof L - 1) == 0);                                  /* too small a buffer */
    /* Two rectangles separated vertically: the first is cancelled before the second starts. */
    static const uint8_t G[] = {0x00,0x2c, 0,0, 0,0, 0,7, 0,6,  0,0, 0,0, 0,2, 0x7f,0xff,  0,2, 0,0, 0,2, 0x7f,0xff,  0,5, 0,3, 0,6, 0x7f,0xff,  0,7, 0,3, 0,6, 0x7f,0xff,  0x7f,0xff};
    CHECK(mac_rgn_decode(G, sizeof G, &g, &why) == 0 && g.n == 2 && g.r[1].t == 5 && g.r[1].l == 3);
    CHECK(mac_rgn_encode(&g, out, sizeof out) == sizeof G && !memcmp(out, G, sizeof G));
    /* A rectangle and an empty region are the 10-byte header. */
    static const uint8_t R[] = {0,10, 0,1, 0,2, 0,5, 0,9}, E0[] = {0,10, 0,0, 0,0, 0,0, 0,0}, E1[] = {0,10, 0,3, 0,3, 0,3, 0,3};
    CHECK(mac_rgn_decode(R, 10, &g, &why) == 0 && g.n == 1 && g.r[0].t == 1 && g.r[0].l == 2 && g.r[0].b == 5 && g.r[0].r == 9);
    CHECK(mac_rgn_encode(&g, out, sizeof out) == 10 && !memcmp(out, R, 10));
    CHECK(mac_rgn_decode(E0, 10, &g, &why) == 0 && g.n == 0 && mac_rgn_encode(&g, out, sizeof out) == 10 && !memcmp(out, E0, 10));
    CHECK(mac_rgn_decode(E1, 10, &g, &why) == 0 && g.n == 0);                            /* an empty box is empty whatever its numbers */
    /* Malformed encodings are refused with a reason. */
    for (unsigned k = 0; k < 14; k++) {
        uint8_t t[sizeof L]; memcpy(t, L, sizeof L); size_t len = sizeof L;
        if (k == 0) len = 8;                                    /* shorter than the header */
        if (k == 1) len = sizeof L - 1;                         /* odd size */
        if (k == 2) t[1] = 0x26;                                /* size field disagrees with the allocation */
        if (k == 3) { t[21] = 0; t[20] = 0; }                   /* second v goes back to 0: not increasing (record 2 starts at byte 20: v) */
        if (k == 4) { t[17] = 3; }                              /* h points no longer ascending: 0, then 3 before 4... still ascending; make 4,0 */
        if (k == 5) { t[15] = 9; t[17] = 2; }                   /* 9 then 2 */
        if (k == 6) { t[sizeof L - 3] = 0x12; }                 /* the final point is not a terminator: unterminated record */
        if (k == 7) { t[sizeof L - 1] = 1; t[sizeof L - 2] = 0; }   /* the closing marker is not $7FFF */
        if (k == 8) { len = sizeof L - 2; t[1] = (uint8_t)len; }    /* the closing marker is missing */
        if (k == 9) { t[19] = 5; }                              /* an odd number of edges on the second record (removes a toggle) */
        if (k == 10) { t[1] = 0x28; len = sizeof L + 4; }       /* data after the terminator (buffer larger) */
        if (k == 11) len = 40000;                               /* far beyond the cap */
        if (k == 12) { t[sizeof L - 7] = 9; }                   /* the bottom record no longer cancels: does not end empty */
        if (k == 13) len = 0;
        uint8_t big[40010]; memset(big, 0, sizeof big); memcpy(big, t, len < sizeof t ? len : sizeof t); if (k == 10) { big[sizeof L - 2] = 0; }
        CHECK(mac_rgn_decode(k == 10 || k == 11 ? big : t, len, &g, &why) == -1 && why && *why);
    }
    CHECK(mac_rgn_decode(NULL, 10, &g, &why) == -1);
    /* Too many edges on one scan line: 600 inversion points (the limit is 512). */
    { static uint8_t m[4000]; size_t at = 10; put16(m + at, 0); at += 2; for (int i = 0; i < 600; i++) { put16(m + at, i * 2); at += 2; } put16(m + at, 0x7fff); at += 2;
      put16(m + at, 5); at += 2; for (int i = 0; i < 600; i++) { put16(m + at, i * 2); at += 2; } put16(m + at, 0x7fff); at += 2; put16(m + at, 0x7fff); at += 2;
      put16(m, (int)at); CHECK(mac_rgn_decode(m, at, &g, &why) == -1 && why); }
    mac_rgn_free(&g);
}
static void limit_checks(void) {
    MacRgn acc, one, r, bands; mac_rgn_init(&acc); mac_rgn_init(&one); mac_rgn_init(&r); mac_rgn_init(&bands); const char *why;
    /* 300 rectangles in one row: the engine holds them, but the encoding refuses (more than 256 in a band). */
    for (int i = 0; i < 300; i++) { mac_rgn_set_rect(&one, (MacRect){0, i * 2, 1, i * 2 + 1}); CHECK(mac_rgn_op(MAC_RGN_OP_UNION, &acc, &one, &r, &why) == 0); mac_rgn_copy(&acc, &r); }
    CHECK(acc.n == 300 && mac_rgn_encoded_size(&acc) == 0 && mac_rgn_encode(&acc, (uint8_t[64]){0}, 64) == 0);
    /* Beyond the rectangle cap the operation fails with a reason and leaves a clean (empty) result. */
    int rc = 0; for (int i = 300; i < 2100 && !rc; i++) { mac_rgn_set_rect(&one, (MacRect){0, i * 2, 1, i * 2 + 1}); rc = mac_rgn_op(MAC_RGN_OP_UNION, &acc, &one, &r, &why); if (!rc) mac_rgn_copy(&acc, &r); }
    CHECK(rc == -1 && why && r.n == 0 && acc.n == MAC_RGN_MAX_RECTS);
    /* Many bands that stay under the byte cap encode; a staircase far beyond it is refused. */
    mac_rgn_set_rect(&acc, (MacRect){0, 0, 0, 0});
    for (int i = 0; i < 40; i++) { mac_rgn_set_rect(&one, (MacRect){i * 2, 0, i * 2 + 1, i + 1}); mac_rgn_op(MAC_RGN_OP_UNION, &acc, &one, &r, &why); mac_rgn_copy(&acc, &r); }
    CHECK(acc.n == 40 && mac_rgn_encoded_size(&acc) > 10 && mac_rgn_encoded_size(&acc) < MAC_RGN_MAX_BYTES);
    uint8_t buf[4096]; MacRgn back; mac_rgn_init(&back); size_t n = mac_rgn_encode(&acc, buf, sizeof buf);
    CHECK(n && mac_rgn_decode(buf, n, &back, &why) == 0 && mac_rgn_equal(&back, &acc));
    mac_rgn_free(&acc); mac_rgn_free(&one); mac_rgn_free(&r); mac_rgn_free(&bands); mac_rgn_free(&back);
}

/* Decoding is a trust boundary: the bytes come from guest memory. Each case below is one specific violation, built byte by byte, and
 * is decoded from an exact-size heap buffer so that any read past its end is an ASan error. */
static int decode_exact(const uint8_t *bytes, size_t len, const char **why) {
    uint8_t *buf = malloc(len ? len : 1); MacRgn g; mac_rgn_init(&g);
    if (len) memcpy(buf, bytes, len);
    int rc = mac_rgn_decode(buf, len, &g, why);
    mac_rgn_free(&g); free(buf); return rc;
}
#define W16(v) (uint8_t)((v) >> 8), (uint8_t)(v)
#define END W16(0x7fff)
static void untrusted_checks(void) {
    const char *why = NULL;
    /* well-formed reference: v=0 edges {1,4}; v=2 cancels them; end. 10 + 8 + 8 + 2 = 28 bytes */
    static const uint8_t good[] = {W16(28), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), W16(4), END,  W16(2), W16(1), W16(4), END,  END};
    CHECK(decode_exact(good, sizeof good, &why) == 0);
    /* an odd number of edges when a band ends: v=0 {1}; v=2 {1}; end */
    static const uint8_t odd_edges[] = {W16(24), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), END,  W16(2), W16(1), END,  END};
    CHECK(decode_exact(odd_edges, sizeof odd_edges, &why) == -1 && why && strstr(why, "odd"));
    /* inversion points not ascending: {4, 0}, and a repeated point {0, 0} */
    static const uint8_t desc[] = {W16(28), W16(0), W16(0), W16(2), W16(4),  W16(0), W16(4), W16(0), END,  W16(2), W16(0), W16(4), END,  END};
    static const uint8_t dup[] = {W16(28), W16(0), W16(0), W16(2), W16(4),  W16(0), W16(0), W16(0), END,  W16(2), W16(0), W16(0), END,  END};
    CHECK(decode_exact(desc, sizeof desc, &why) == -1 && why && strstr(why, "ascending"));
    CHECK(decode_exact(dup, sizeof dup, &why) == -1 && why && strstr(why, "ascending"));
    /* scan lines not increasing: v=2 then v=2 again, and v=3 then v=1 */
    static const uint8_t same_v[] = {W16(28), W16(0), W16(1), W16(2), W16(4),  W16(2), W16(1), W16(4), END,  W16(2), W16(1), W16(4), END,  END};
    static const uint8_t back_v[] = {W16(28), W16(0), W16(1), W16(2), W16(4),  W16(3), W16(1), W16(4), END,  W16(1), W16(1), W16(4), END,  END};
    CHECK(decode_exact(same_v, sizeof same_v, &why) == -1 && why && strstr(why, "increasing"));
    CHECK(decode_exact(back_v, sizeof back_v, &why) == -1 && why && strstr(why, "increasing"));
    /* the list runs out: no scan-line word where one is needed (the closing $7FFF is missing), and a point list with no terminator */
    static const uint8_t no_close[] = {W16(26), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), W16(4), END,  W16(2), W16(1), W16(4), END};
    static const uint8_t no_term[] = {W16(16), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), W16(4)};
    CHECK(decode_exact(no_close, sizeof no_close, &why) == -1 && why && strstr(why, "run past"));
    CHECK(decode_exact(no_term, sizeof no_term, &why) == -1 && why && strstr(why, "not terminated"));
    /* data after the final terminator, and a region whose last row is never cancelled */
    static const uint8_t trailing[] = {W16(30), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), W16(4), END,  W16(2), W16(1), W16(4), END,  END, W16(0)};
    static const uint8_t open_end[] = {W16(20), W16(0), W16(1), W16(2), W16(4),  W16(0), W16(1), W16(4), END,  END};
    CHECK(decode_exact(trailing, sizeof trailing, &why) == -1 && why && strstr(why, "after"));
    CHECK(decode_exact(open_end, sizeof open_end, &why) == -1 && why && strstr(why, "empty scan line"));
    /* sizes: shorter than a header, odd, a size word that disagrees, an empty buffer, and a size word of exactly the cap + 8 */
    CHECK(decode_exact(good, 8, &why) == -1 && decode_exact(good, 0, &why) == -1 && decode_exact(good, 27, &why) == -1);
    { uint8_t t[sizeof good]; memcpy(t, good, sizeof t); t[1] = 30; CHECK(decode_exact(t, sizeof t, &why) == -1 && why && strstr(why, "size field")); }
    /* a hostile size word and length far beyond the cap */
    { size_t len = 40000; uint8_t *b = calloc(len, 1); put16(b, (int)len); MacRgn g; mac_rgn_init(&g); CHECK(mac_rgn_decode(b, len, &g, &why) == -1 && why && strstr(why, "size")); mac_rgn_free(&g); free(b); }
    /* a size-10 header whose box is inverted is simply empty */
    static const uint8_t inverted[] = {W16(10), W16(9), W16(9), W16(2), W16(2)};
    { MacRgn g; mac_rgn_init(&g); CHECK(mac_rgn_decode(inverted, 10, &g, &why) == 0 && g.n == 0); mac_rgn_free(&g); }
}
static void rect_query_checks(void) {
    MacRgn g; mac_rgn_init(&g); MacRect r = {0, 0, 0, 0};
    CHECK(!mac_rgn_is_rect(&g, &r) && !mac_rgn_is_rect(&g, NULL));                       /* empty */
    mac_rgn_set_rect(&g, (MacRect){2, 3, 7, 9});
    CHECK(mac_rgn_is_rect(&g, &r) && r.t == 2 && r.l == 3 && r.b == 7 && r.r == 9 && mac_rgn_is_rect(&g, NULL));
    MacRgn b, o; mac_rgn_init(&b); mac_rgn_init(&o); const char *why; mac_rgn_set_rect(&b, (MacRect){10, 0, 12, 2});
    CHECK(mac_rgn_op(MAC_RGN_OP_UNION, &g, &b, &o, &why) == 0 && !mac_rgn_is_rect(&o, &r) && o.n == 2);       /* two rectangles */
    mac_rgn_free(&g); mac_rgn_free(&b); mac_rgn_free(&o);
}
static void band_cap_checks(void) {
    /* many separate bands: the rectangle cap is reached through bands, not through one wide row */
    MacRgn acc, one, r; mac_rgn_init(&acc); mac_rgn_init(&one); mac_rgn_init(&r); const char *why; int failed_at = -1;
    for (int i = 0; i < 2100; i++) {
        mac_rgn_set_rect(&one, (MacRect){2 * i, 0, 2 * i + 1, 1});
        if (mac_rgn_op(MAC_RGN_OP_UNION, &acc, &one, &r, &why)) { failed_at = i; break; }
        mac_rgn_copy(&acc, &r);
    }
    CHECK(failed_at == MAC_RGN_MAX_RECTS && acc.n == MAC_RGN_MAX_RECTS && r.n == 0 && why && strstr(why, "too many"));
    mac_rgn_free(&acc); mac_rgn_free(&one); mac_rgn_free(&r);
}
int main(void) {
    oracle_checks(); untrusted_checks(); rect_query_checks(); band_cap_checks(); byte_checks(); limit_checks();
    if (!fails) puts("Region engine checks passed");
    return fails ? 1 : 0;
}
