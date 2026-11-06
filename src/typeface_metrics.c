#include "draw.h"

int typeface_width(int body, const char *s, int nbytes) {
    if (!s) {
        return 0;
    }
    int used = 0;
    int px = 0;
    while (*s && (nbytes < 0 || used < nbytes)) {
        int b = 0;
        int w = 0;
        if (!typeface_next(body, s, &b, &w) || b <= 0) {
            break;
        }
        if (nbytes >= 0 && used + b > nbytes) {
            break;
        }
        used += b;
        s += b;
        px += w;
    }
    return px;
}
