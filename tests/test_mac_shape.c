/* Our own oval and round-rectangle scan conversion (mac_shape.c): hand-computed small shapes and invariants. */
#include "mac_shape.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
static int rows_are(const MacShape *s, int n, const int (*lh)[2]) {
    if (s->b - s->t != n) return 0;
    for (int i = 0; i < n; i++) if (s->lo[i] != lh[i][0] || s->hi[i] != lh[i][1]) return 0;
    return 1;
}
int main(void) {
    MacShape s;
    /* hand-computed: a 4 x 4 oval is 2, 4, 4, 2 wide; a 3 x 3 oval is 1, 3, 1; 1 x 1 and 2 x 2 are solid */
    CHECK(mac_shape_build(&s, 10, 20, 14, 24, 4, 4) == 0);
    {static const int e[4][2] = {{21,23},{20,24},{20,24},{21,23}}; CHECK(rows_are(&s, 4, e));} mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 0, 0, 3, 3, 3, 3) == 0);
    {static const int e[3][2] = {{1,2},{0,3},{1,2}}; CHECK(rows_are(&s, 3, e));} mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 5, 5, 6, 6, 1, 1) == 0 && s.b - s.t == 1 && s.lo[0] == 5 && s.hi[0] == 6); mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 0, 0, 2, 2, 2, 2) == 0 && s.lo[0] == 0 && s.hi[0] == 2 && s.lo[1] == 0 && s.hi[1] == 2); mac_shape_free(&s);
    /* an 8 x 4 oval: widths 4, 8, 8, 4 */
    CHECK(mac_shape_build(&s, 0, 0, 4, 8, 8, 4) == 0);
    {static const int e[4][2] = {{2,6},{0,8},{0,8},{2,6}}; CHECK(rows_are(&s, 4, e));} mac_shape_free(&s);
    /* a 10 x 10 round rectangle with 4 x 4 corners: the corner ellipse's first rows inset 1 and 0 */
    CHECK(mac_shape_build(&s, 0, 0, 10, 10, 4, 4) == 0);
    {static const int e[10][2] = {{1,9},{0,10},{0,10},{0,10},{0,10},{0,10},{0,10},{0,10},{0,10},{1,9}}; CHECK(rows_are(&s, 10, e));}
    CHECK(mac_shape_contains(&s, 0, 0) == 0 && mac_shape_contains(&s, 1, 0) == 1 && mac_shape_contains(&s, 9, 9) == 0 && mac_shape_contains(&s, 8, 9) == 1 &&
          mac_shape_contains(&s, -1, 5) == 0 && mac_shape_contains(&s, 10, 5) == 0 && mac_shape_contains(&s, 5, -1) == 0 && mac_shape_contains(&s, 5, 10) == 0);
    mac_shape_free(&s);
    /* corners of zero size, or larger than the rectangle, or negative */
    for (int k = 0; k < 4; k++) {
        int ow = k == 0 ? 0 : k == 1 ? -5 : k == 2 ? 100 : 6, oh = k == 3 ? 0 : k == 1 ? 3 : 100;
        CHECK(mac_shape_build(&s, 0, 0, 6, 6, ow, oh) == 0 && s.b == 6);
        if (k == 0 || k == 1 || k == 3) for (int i = 0; i < 6; i++) CHECK(s.lo[i] == 0 && s.hi[i] == 6);     /* plain rectangle */
        else { CHECK(s.lo[0] > 0 && s.lo[2] == 0 && s.lo[5] == s.lo[0]); }                                    /* clamped to the rectangle: an oval */
        mac_shape_free(&s);
    }
    /* corner ellipses larger than the box clamp to it: the same rows as the oval of that box, exactly */
    for(int w=2;w<=30;w++)for(int h=2;h<=30;h++){MacShape o,c;CHECK(mac_shape_build(&o,0,0,h,w,w,h)==0&&mac_shape_build(&c,0,0,h,w,1000,1000)==0);for(int i=0;i<h;i++)CHECK(o.lo[i]==c.lo[i]&&o.hi[i]==c.hi[i]);mac_shape_free(&c);mac_shape_free(&o);}
    /* empty and inverted boxes have no rows; too tall is refused */
    CHECK(mac_shape_build(&s, 5, 5, 5, 9, 2, 2) == 0 && s.b - s.t == 0 && !mac_shape_contains(&s, 6, 5)); mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 5, 9, 8, 5, 2, 2) == 0 && s.b - s.t == 0); mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 0, 0, 2048, 10, 10, 2048) == 0 && s.b == 2048); mac_shape_free(&s);
    CHECK(mac_shape_build(&s, 0, 0, 2049, 10, 10, 2049) == -1 && s.lo == NULL && s.hi == NULL);
    CHECK(mac_shape_build(&s, -32768, -32768, 32767, 32767, 10, 10) == -1);
    /* invariants over many sizes: symmetric, inside the box, widths of the box's parity, never narrower toward the middle, area close to pi/4 w h */
    for (int w = 1; w <= 60; w++) for (int h = 1; h <= 60; h++) {
        CHECK(mac_shape_build(&s, 3, 7, 3 + h, 7 + w, w, h) == 0);
        long area = 0; int ok = 1;
        for (int i = 0; i < h; i++) {
            int lo = s.lo[i] - 7, hi = s.hi[i] - 7, width = hi - lo;
            if (lo < 0 || hi > w || width < 1 || ((w - width) & 1) || lo != w - hi) ok = 0;                      /* symmetric left to right */
            if (s.lo[i] != s.lo[h - 1 - i] || s.hi[i] != s.hi[h - 1 - i]) ok = 0;                                /* and top to bottom */
            if (i > 0 && i <= (h - 1) / 2 && width < s.hi[i - 1] - s.lo[i - 1]) ok = 0;                          /* widening to the middle */
            area += width;
        }
        CHECK(ok);
        if (w >= 16 && h >= 16) { double want = 3.14159265358979 * w * h / 4.0; CHECK(fabs((double)area - want) <= (w >= 32 && h >= 32 ? 0.03 : 0.06) * want); }   /* the parity rule costs a few percent on small shapes */
        mac_shape_free(&s);
    }
    mac_shape_free(&s);
    if (!fails) puts("Shape scan conversion checks passed");
    return fails ? 1 : 0;
}
