/* Folding-phone profiles and posture detection.
 *
 * Profiles drive the simulator, the screenshot tool and the tests. A running
 * app never trusts them: it uses what the platform reports (Jetpack
 * WindowManager on Android, scene geometry on iOS). Sizes are drawable pixels
 * with the cover screen held upright and the inner screen in its natural
 * orientation.
 */
#include "mobile.h"

#include <math.h>
#include <string.h>

static const MobileDeviceProfile profiles[] = {
    /* id, name, cover w h ppi, inner w h ppi, flip, rumored */
    {"pixel-10-pro-fold", "Pixel 10 Pro Fold", 1080, 2364, 422.f, 2076, 2152, 373.f, 0, 0, 0},
    {"galaxy-z-fold8", "Galaxy Z Fold8", 1080, 2520, 422.f, 1968, 2184, 368.f, 0, 0, 0},
    {"iphone-fold", "iPhone Fold (rumored specs)", 1422, 2088, 460.f, 2713, 1920, 428.f, 0, 1, 0},
    {"oneplus-open", "OnePlus Open", 1116, 2484, 431.f, 2268, 2440, 426.f, 0, 0, 0},
    {"galaxy-z-flip7", "Galaxy Z Flip7", 948, 1048, 345.f, 1080, 2520, 397.f, 1, 0, 0},
    {"razr-ultra", "motorola razr ultra", 1272, 1080, 417.f, 1224, 2992, 464.f, 1, 0, 0},
    /* Tablets: no cover, no hinge. Pair them with a keyboard case, a folio or
     * any Bluetooth keyboard; a trackpad or mouse drives the OS999 pointer. */
    {"ipad-pro-13", "iPad Pro 13-inch", 0, 0, 0, 2752, 2064, 264.f, 0, 0, 1},
    {"ipad-air-11", "iPad Air 11-inch", 0, 0, 0, 2360, 1640, 264.f, 0, 0, 1},
    {"ipad-mini", "iPad mini", 0, 0, 0, 2266, 1488, 326.f, 0, 0, 1},
    {"galaxy-tab-s10-ultra", "Galaxy Tab S10 Ultra", 0, 0, 0, 2960, 1848, 239.f, 0, 0, 1},
    {"pixel-tablet", "Pixel Tablet", 0, 0, 0, 2560, 1600, 276.f, 0, 0, 1},
};

int mobile_device_count(void) { return (int)(sizeof profiles / sizeof *profiles); }

const MobileDeviceProfile *mobile_device_at(int index) {
    if (index < 0 || index >= mobile_device_count()) return NULL;
    return &profiles[index];
}

const MobileDeviceProfile *mobile_device_find(const char *id) {
    for (int i = 0; id && i < mobile_device_count(); i++)
        if (strcmp(profiles[i].id, id) == 0) return &profiles[i];
    return NULL;
}

static void phone_insets(MobileScreen *s) {
    float dp = s->ppi / 160.0f;
    s->inset_t = (int)lroundf(28 * dp);   /* status bar / camera cut-out */
    s->inset_b = (int)lroundf(14 * dp);   /* gesture handle */
    s->inset_l = s->inset_r = 0;
}

void mobile_device_screen(const MobileDeviceProfile *d, MobilePosture p, MobileScreen *out) {
    memset(out, 0, sizeof *out);
    if (!d) return;
    if (d->tablet) {   /* every posture is the one landscape screen */
        float dp = d->inner_ppi / 160.0f;
        out->w = d->inner_w; out->h = d->inner_h; out->ppi = d->inner_ppi;
        out->inner = 1;
        out->tablet = 1;
        out->inset_t = (int)lroundf(8 * dp);
        out->inset_b = (int)lroundf(16 * dp);   /* home indicator */
        return;
    }
    if (p == MP_COVER || p == MP_AUTO) {
        out->w = d->cover_w; out->h = d->cover_h; out->ppi = d->cover_ppi;
        out->inner = 0;
        phone_insets(out);
        return;
    }
    out->ppi = d->inner_ppi;
    out->inner = 1;
    out->w = d->inner_w; out->h = d->inner_h;
    /* Book-style phones hinge down the middle of the inner screen (vertical
     * line); flip phones hinge across it (horizontal line). */
    int hinge_vertical = !d->flip;
    if (p == MP_LAPTOP && hinge_vertical) {   /* turn the phone so the hinge is level */
        out->w = d->inner_h; out->h = d->inner_w;
        hinge_vertical = 0;
    }
    if (p == MP_BOOK && !hinge_vertical) p = MP_FLAT;   /* a flip has no book posture */
    if (hinge_vertical) {
        out->fold_axis = MF_AXIS_VERTICAL;
        out->fold = (Rect){out->w / 2, 0, 0, out->h};
    } else {
        out->fold_axis = MF_AXIS_HORIZONTAL;
        out->fold = (Rect){0, out->h / 2, out->w, 0};
    }
    out->fold_state = (p == MP_LAPTOP || p == MP_BOOK) ? MF_STATE_HALF : MF_STATE_FLAT;
    phone_insets(out);
    if (out->w > out->h) {                    /* landscape: bars move to the sides */
        out->inset_l = out->inset_t;
        out->inset_t = out->inset_b = (int)lroundf(8 * out->ppi / 160.0f);
    }
}

MobilePosture mobile_posture_detect(const MobileScreen *s) {
    if (!s || s->w <= 0 || s->h <= 0) return MP_COVER;
    if (s->fold_state == MF_STATE_HALF)
        return s->fold_axis == MF_AXIS_HORIZONTAL ? MP_LAPTOP : MP_BOOK;
    if (s->inner == 1) return MP_FLAT;
    if (s->inner == 0) return MP_COVER;
    /* Unknown screen (no fold API yet): squarish means the big screen. */
    float lo = (float)(s->w < s->h ? s->w : s->h), hi = (float)(s->w < s->h ? s->h : s->w);
    return lo / hi >= 0.70f ? MP_FLAT : MP_COVER;
}

const char *mobile_posture_name(MobilePosture p) {
    switch (p) {
    case MP_COVER: return "Cover";
    case MP_FLAT: return "Unfolded";
    case MP_LAPTOP: return "Laptop";
    case MP_BOOK: return "Book";
    default: return "Automatic";
    }
}

const char *mobile_posture_hint(MobilePosture p) {
    switch (p) {
    case MP_COVER: return "Cover screen: desktop above, keyboard below.";
    case MP_FLAT: return "Unfolded: the whole desktop, same windows.";
    case MP_LAPTOP: return "Laptop mode: keyboard and trackpad below the fold.";
    case MP_BOOK: return "Book mode: nothing sits on the crease.";
    default: return "Postures follow the hinge.";
    }
}
