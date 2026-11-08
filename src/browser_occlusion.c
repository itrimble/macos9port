#include "browser_overlay.h"
#include <stdlib.h>

static int compare_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

int overlay_visible_regions(OverlayRect bounds, const OverlayRect *holes, int count,
                            void (*emit)(OverlayRect, void *), void *context) {
    enum { MAX_HOLES = 64 };
    if (!emit || count < 0 || count > MAX_HOLES || (count && !holes) ||
        bounds.w <= 0 || bounds.h <= 0 || bounds.x < 0 || bounds.y < 0 ||
        (int64_t)bounds.x + bounds.w > 16384 || (int64_t)bounds.y + bounds.h > 16384)
        return -1;
    int xs[MAX_HOLES * 2 + 2], ys[MAX_HOLES * 2 + 2], nx = 2, ny = 2, n = 0;
    OverlayRect clipped[MAX_HOLES];
    xs[0] = bounds.x; xs[1] = bounds.x + bounds.w;
    ys[0] = bounds.y; ys[1] = bounds.y + bounds.h;
    for (int i = 0; i < count; i++) {
        OverlayRect r = holes[i];
        if (r.w <= 0 || r.h <= 0) continue;
        int64_t right = (int64_t)r.x + r.w, bottom = (int64_t)r.y + r.h;
        int left = r.x > bounds.x ? r.x : bounds.x;
        int top = r.y > bounds.y ? r.y : bounds.y;
        if (right > xs[1]) right = xs[1];
        if (bottom > ys[1]) bottom = ys[1];
        if (right <= left || bottom <= top) continue;
        clipped[n++] = (OverlayRect){left, top, (int)right - left, (int)bottom - top};
        xs[nx++] = left; xs[nx++] = (int)right;
        ys[ny++] = top; ys[ny++] = (int)bottom;
    }
    qsort(xs, (size_t)nx, sizeof *xs, compare_int);
    qsort(ys, (size_t)ny, sizeof *ys, compare_int);
    /* Each cell is either entirely hidden or entirely visible. Unlike an
     * even-odd path of holes, overlapping holes cannot open a visible island. */
    for (int x = 0; x + 1 < nx; x++) {
        if (xs[x] == xs[x + 1]) continue;
        int start = -1;
        for (int y = 0; y + 1 < ny; y++) {
            if (ys[y] == ys[y + 1]) continue;
            int hidden = 0;
            for (int i = 0; i < n && !hidden; i++) {
                OverlayRect r = clipped[i];
                hidden = xs[x] >= r.x && xs[x] < r.x + r.w &&
                         ys[y] >= r.y && ys[y] < r.y + r.h;
            }
            if (!hidden && start < 0) start = ys[y];
            if (hidden && start >= 0) {
                emit((OverlayRect){xs[x], start, xs[x + 1] - xs[x], ys[y] - start}, context);
                start = -1;
            }
        }
        if (start >= 0)
            emit((OverlayRect){xs[x], start, xs[x + 1] - xs[x], ys[ny - 1] - start}, context);
    }
    return 0;
}
