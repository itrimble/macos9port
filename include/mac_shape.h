#ifndef MAC_SHAPE_H
#define MAC_SHAPE_H
/* Ovals and round rectangles as one span per row.
 *
 * THIS IS OUR OWN SCAN CONVERSION, not QuickDraw's: the rows come from the integer ellipse x^2/a^2 + y^2/b^2 = 1 sampled at each row's
 * centre and rounded to the nearest width with the parity of the shape (so it is symmetric left to right), ties going to the narrower
 * span. Pixels near the curves can therefore differ from a real Mac. A round rectangle has corners that are quarter ellipses of
 * ovalWidth x ovalHeight (clamped to the rectangle); an oval is a round rectangle whose corner ellipse is the whole rectangle. */
enum { MAC_SHAPE_MAX_ROWS = 2048 };
typedef struct { int t, l, b, r; int *lo, *hi; } MacShape;     /* row i (y = t + i) covers x in [lo[i], hi[i]) */
void mac_shape_init(MacShape *s);
void mac_shape_free(MacShape *s);
/* Builds the shape of the box (top, left, bottom, right) with corner ellipses ow x oh (0 for either = a plain rectangle). An empty or
 * inverted box gives zero rows. 0 on success, -1 if the box is taller than MAC_SHAPE_MAX_ROWS or memory runs out. */
int mac_shape_build(MacShape *s, int t, int l, int b, int r, int ow, int oh);
int mac_shape_contains(const MacShape *s, int x, int y);
#endif
