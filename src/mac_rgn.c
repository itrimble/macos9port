#include "mac_rgn.h"
#include <stdlib.h>
#include <string.h>

/* The region engine: see include/mac_rgn.h. Everything works on bands. A band is a run of scan lines [t, b) over which the set of
 * covered x intervals does not change; a region is a list of bands, kept canonical by merging touching intervals and identical
 * consecutive bands. */
void mac_rgn_init(MacRgn *g) { g->r = NULL; g->n = 0; g->cap = 0; }
void mac_rgn_free(MacRgn *g) { free(g->r); mac_rgn_init(g); }
static int reserve(MacRgn *g, int need) {
    if (need <= g->cap) return 0;
    int cap = g->cap ? g->cap : 8; while (cap < need) cap *= 2;
    MacRect *p = realloc(g->r, (size_t)cap * sizeof *p);
    if (!p) return -1;
    g->r = p; g->cap = cap; return 0;
}
static int push(MacRgn *g, MacRect r) {
    if (g->n >= MAC_RGN_MAX_RECTS) return -1;
    if (reserve(g, g->n + 1)) return -1;
    g->r[g->n++] = r; return 0;
}
int mac_rgn_set_rect(MacRgn *g, MacRect r) {
    g->n = 0;
    if (r.b <= r.t || r.r <= r.l) return 0;
    return push(g, r);
}
int mac_rgn_copy(MacRgn *dst, const MacRgn *src) {
    if (reserve(dst, src->n)) return -1;
    memcpy(dst->r, src->r, (size_t)src->n * sizeof *src->r); dst->n = src->n; return 0;
}

/* A builder that takes bands in increasing order and keeps the region canonical: intervals handed in are sorted and may touch; a band
 * identical to the previous one that touches it extends it. */
typedef struct { MacRgn *g; int last_start, last_n, last_b; } Builder;
static void builder_init(Builder *b, MacRgn *g) { b->g = g; g->n = 0; b->last_start = 0; b->last_n = 0; b->last_b = 0; }
static int add_band(Builder *bd, int t, int b, const int (*iv)[2], int n) {
    if (b <= t || n == 0) return 0;
    /* merge touching or overlapping intervals into a temporary list */
    int tmp[MAC_RGN_MAX_RECTS][2], m = 0;
    for (int i = 0; i < n; i++) {
        if (iv[i][1] <= iv[i][0]) continue;
        if (m && iv[i][0] <= tmp[m - 1][1]) { if (iv[i][1] > tmp[m - 1][1]) tmp[m - 1][1] = iv[i][1]; }
        else { if (m >= MAC_RGN_MAX_RECTS) return -1; tmp[m][0] = iv[i][0]; tmp[m][1] = iv[i][1]; m++; }
    }
    if (!m) return 0;
    MacRgn *g = bd->g;
    if (bd->last_n == m && bd->last_b == t) {
        int same = 1;
        for (int i = 0; i < m; i++) if (g->r[bd->last_start + i].l != tmp[i][0] || g->r[bd->last_start + i].r != tmp[i][1]) { same = 0; break; }
        if (same) { for (int i = 0; i < m; i++) g->r[bd->last_start + i].b = b; bd->last_b = b; return 0; }
    }
    bd->last_start = g->n; bd->last_n = m; bd->last_b = b;
    for (int i = 0; i < m; i++) if (push(g, (MacRect){t, tmp[i][0], b, tmp[i][1]})) return -1;
    return 0;
}

/* ---- decoding and encoding ---- */
enum { MAX_BAND = 256 };   /* rectangles in one band: 512 edges, the decoder's limit and the size of the encoder's edge buffers */
static unsigned be16(const uint8_t *p) { return (unsigned)(p[0] << 8 | p[1]); }
int mac_rgn_decode(const uint8_t *d, size_t size, MacRgn *out, const char **why) {
    *why = NULL; out->n = 0;
    if (!d || size < 10 || size > MAC_RGN_MAX_BYTES + 6 || (size & 1)) { *why = "Region record has an invalid size."; return -1; }
    if (be16(d) != size) { *why = "Region record's size field disagrees with its allocation."; return -1; }
    MacRect bb = {(int16_t)be16(d + 2), (int16_t)be16(d + 4), (int16_t)be16(d + 6), (int16_t)be16(d + 8)};
    if (size == 10) { if (bb.b <= bb.t || bb.r <= bb.l) return 0; return mac_rgn_set_rect(out, bb); }
    Builder bd; builder_init(&bd, out);
    int edges[512], ne = 0, prev_v = 0, have = 0, band_t = 0;
    size_t at = 10;
    for (;;) {
        if (at + 2 > size) { *why = "Region scan lines run past the record."; return -1; }
        int v = (int16_t)be16(d + at); at += 2;
        if (v == 0x7fff) { if (at != size) { *why = "Region has data after its terminator."; return -1; } break; }
        if (have && v <= prev_v) { *why = "Region scan lines are not in increasing order."; return -1; }
        if (have && ne) {                                   /* the band that ends at v */
            int iv[512][2], m = 0;
            if (ne & 1) { *why = "Region scan line has an odd number of edges."; return -1; }
            for (int i = 0; i + 1 < ne; i += 2) { iv[m][0] = edges[i]; iv[m][1] = edges[i + 1]; m++; }
            if (add_band(&bd, band_t, v, (const int (*)[2])iv, m)) { *why = "Region has too many rectangles."; return -1; }
        }
        int last = -32768;
        for (;;) {
            if (at + 2 > size) { *why = "Region scan line is not terminated."; return -1; }
            int h = (int16_t)be16(d + at); at += 2;
            if (h == 0x7fff) break;
            if (h <= last) { *why = "Region inversion points are not ascending."; return -1; }
            last = h;
            int k = 0; while (k < ne && edges[k] < h) k++;  /* toggle h in the sorted edge set */
            if (k < ne && edges[k] == h) { memmove(edges + k, edges + k + 1, (size_t)(ne - k - 1) * sizeof *edges); ne--; }
            else { if (ne >= 512) { *why = "Region scan line has too many edges."; return -1; } memmove(edges + k + 1, edges + k, (size_t)(ne - k) * sizeof *edges); edges[k] = h; ne++; }
        }
        prev_v = v; have = 1; band_t = v;
    }
    if (ne) { *why = "Region does not end with an empty scan line."; return -1; }
    (void)bb;
    return 0;
}

/* The bands of a canonical region: starting index and count of each run of rectangles sharing a top. */
static int next_band(const MacRgn *g, int i, int *j) { int t = g->r[i].t; *j = i; while (*j < g->n && g->r[*j].t == t) (*j)++; return t; }
size_t mac_rgn_encoded_size(const MacRgn *g) {
    if (g->n == 0 || g->n == 1) return 10;
    size_t total = 10 + 2; int prev_b = 0, prev_n = 0, i = 0;
    while (i < g->n) {
        int j; int t = next_band(g, i, &j), n = j - i;
        if (n > MAX_BAND) return 0;                                                       /* the encoding's fixed edge buffers */
        if (prev_n && prev_b != t) total += 4 + 2 * (size_t)(2 * prev_n);                /* a cancelling record for a gap between bands */
        /* points in this record: edges XOR previous (when touching) else all edges; count exactly by merging */
        int cnt = 2 * n;
        if (prev_n && prev_b == t) {
            int a = 0, b = 0, same = 0; const MacRect *p = g->r + i - prev_n, *q = g->r + i;
            int pe[1024], qe[1024]; int np = 0, nq = 0;
            for (int k = 0; k < prev_n; k++) { pe[np++] = p[k].l; pe[np++] = p[k].r; }
            for (int k = 0; k < n; k++) { qe[nq++] = q[k].l; qe[nq++] = q[k].r; }
            while (a < np && b < nq) { if (pe[a] == qe[b]) { same++; a++; b++; } else if (pe[a] < qe[b]) a++; else b++; }
            cnt = np + nq - 2 * same;
        }
        total += 4 + 2 * (size_t)cnt; prev_b = g->r[i].b; prev_n = n; i = j;
        if (total > MAC_RGN_MAX_BYTES) return 0;
    }
    total += 4 + 2 * (size_t)(2 * prev_n);                                                /* the bottom record */
    return total > MAC_RGN_MAX_BYTES ? 0 : total;
}
static void put16(uint8_t *p, int v) { p[0] = (uint8_t)((unsigned)v >> 8); p[1] = (uint8_t)v; }
size_t mac_rgn_encode(const MacRgn *g, uint8_t *out, size_t cap) {
    size_t need = mac_rgn_encoded_size(g);
    if (!need || need > cap) return 0;
    MacRect bb; mac_rgn_bbox(g, &bb);
    put16(out, (int)need); put16(out + 2, bb.t); put16(out + 4, bb.l); put16(out + 6, bb.b); put16(out + 8, bb.r);
    if (need == 10) return 10;
    size_t at = 10; int prev_n = 0, prev_b = 0, i = 0, pe[1024], np = 0;
    while (i < g->n) {
        int j; int t = next_band(g, i, &j), n = j - i, qe[1024], nq = 0;
        if (n > MAX_BAND) return 0;
        for (int k = 0; k < n; k++) { qe[nq++] = g->r[i + k].l; qe[nq++] = g->r[i + k].r; }
        if (prev_n && prev_b != t) {                                       /* cancel the previous band across a gap */
            put16(out + at, prev_b); at += 2; for (int k = 0; k < np; k++) { put16(out + at, pe[k]); at += 2; } put16(out + at, 0x7fff); at += 2;
            np = 0; prev_n = 0;
        }
        put16(out + at, t); at += 2;
        int a = 0, b = 0;                                                  /* the symmetric difference of the previous and this band's edges */
        while (a < np || b < nq) {
            int h;
            if (b >= nq || (a < np && pe[a] < qe[b])) h = pe[a++];
            else if (a >= np || qe[b] < pe[a]) h = qe[b++];
            else { a++; b++; continue; }
            put16(out + at, h); at += 2;
        }
        put16(out + at, 0x7fff); at += 2;
        memcpy(pe, qe, (size_t)nq * sizeof *qe); np = nq; prev_n = n; prev_b = g->r[i].b; i = j;
    }
    put16(out + at, prev_b); at += 2; for (int k = 0; k < np; k++) { put16(out + at, pe[k]); at += 2; } put16(out + at, 0x7fff); at += 2;
    put16(out + at, 0x7fff); at += 2;
    return at == need ? at : 0;
}

/* ---- algebra ---- */
static int cmp_int(const void *a, const void *b) { int x = *(const int *)a, y = *(const int *)b; return (x > y) - (x < y); }
/* Intervals of g covering scan line y, in increasing x order (the canonical list is sorted by top then left, so within a band they are). */
static int row_intervals(const MacRgn *g, int y, int (*iv)[2]) {
    int m = 0;
    for (int i = 0; i < g->n; i++) { if (g->r[i].t > y) break; if (g->r[i].b > y) { iv[m][0] = g->r[i].l; iv[m][1] = g->r[i].r; m++; } }
    return m;
}
static int combine(int op, const int (*a)[2], int na, const int (*b)[2], int nb, int (*out)[2]) {
    int xs[2 * MAC_RGN_MAX_RECTS * 2], nx = 0;
    for (int i = 0; i < na; i++) { xs[nx++] = a[i][0]; xs[nx++] = a[i][1]; }
    for (int i = 0; i < nb; i++) { xs[nx++] = b[i][0]; xs[nx++] = b[i][1]; }
    qsort(xs, (size_t)nx, sizeof *xs, cmp_int);
    int m = 0, ia = 0, ib = 0;
    for (int k = 0; k + 1 < nx; k++) {
        if (xs[k] == xs[k + 1]) continue;
        int x0 = xs[k], x1 = xs[k + 1];
        while (ia < na && a[ia][1] <= x0) ia++;
        while (ib < nb && b[ib][1] <= x0) ib++;
        int in_a = ia < na && a[ia][0] <= x0 && a[ia][1] >= x1, in_b = ib < nb && b[ib][0] <= x0 && b[ib][1] >= x1, in;
        in = op == MAC_RGN_OP_SECT ? (in_a && in_b) : op == MAC_RGN_OP_UNION ? (in_a || in_b) : op == MAC_RGN_OP_DIFF ? (in_a && !in_b) : (in_a != in_b);
        if (!in) continue;
        if (m && out[m - 1][1] == x0) out[m - 1][1] = x1; else { out[m][0] = x0; out[m][1] = x1; m++; }
    }
    return m;
}
int mac_rgn_op(int op, const MacRgn *a, const MacRgn *b, MacRgn *out, const char **why) {
    *why = NULL; out->n = 0;
    int *ys = malloc(sizeof(int) * (size_t)(2 * (a->n + b->n) + 1)); int ny = 0;
    if (!ys) { *why = "Region operation ran out of memory."; return -1; }
    for (int i = 0; i < a->n; i++) { ys[ny++] = a->r[i].t; ys[ny++] = a->r[i].b; }
    for (int i = 0; i < b->n; i++) { ys[ny++] = b->r[i].t; ys[ny++] = b->r[i].b; }
    qsort(ys, (size_t)ny, sizeof *ys, cmp_int);
    static int ia[MAC_RGN_MAX_RECTS][2], ib[MAC_RGN_MAX_RECTS][2], io[2 * MAC_RGN_MAX_RECTS][2];
    Builder bd; builder_init(&bd, out);
    int rc = 0;
    for (int k = 0; k + 1 < ny && !rc; k++) {
        if (ys[k] == ys[k + 1]) continue;
        int na = row_intervals(a, ys[k], ia), nb = row_intervals(b, ys[k], ib);
        int m = combine(op, ia, na, ib, nb, io);
        if (m && add_band(&bd, ys[k], ys[k + 1], (const int (*)[2])io, m)) rc = -1;
    }
    free(ys);
    if (rc) { *why = "Region operation has too many rectangles."; out->n = 0; }
    return rc;
}

/* ---- queries ---- */
int mac_rgn_contains(const MacRgn *g, int x, int y) {
    for (int i = 0; i < g->n; i++) { if (g->r[i].t > y) break; if (y < g->r[i].b && x >= g->r[i].l && x < g->r[i].r) return 1; }
    return 0;
}
int mac_rgn_rect_in(const MacRgn *g, MacRect q) {
    if (q.b <= q.t || q.r <= q.l) return 0;
    for (int i = 0; i < g->n; i++) if (g->r[i].t < q.b && g->r[i].b > q.t && g->r[i].l < q.r && g->r[i].r > q.l) return 1;
    return 0;
}
int mac_rgn_is_empty(const MacRgn *g) { return g->n == 0; }
int mac_rgn_is_rect(const MacRgn *g, MacRect *r) { if (g->n != 1) return 0; if (r) *r = g->r[0]; return 1; }
void mac_rgn_bbox(const MacRgn *g, MacRect *r) {
    if (!g->n) { *r = (MacRect){0, 0, 0, 0}; return; }
    *r = g->r[0];
    for (int i = 1; i < g->n; i++) { if (g->r[i].t < r->t) r->t = g->r[i].t; if (g->r[i].l < r->l) r->l = g->r[i].l; if (g->r[i].b > r->b) r->b = g->r[i].b; if (g->r[i].r > r->r) r->r = g->r[i].r; }
}
int mac_rgn_equal(const MacRgn *a, const MacRgn *b) { return a->n == b->n && (a->n == 0 || !memcmp(a->r, b->r, (size_t)a->n * sizeof *a->r)); }
void mac_rgn_offset(MacRgn *g, int dh, int dv) { for (int i = 0; i < g->n; i++) { g->r[i].t += dv; g->r[i].b += dv; g->r[i].l += dh; g->r[i].r += dh; } }
