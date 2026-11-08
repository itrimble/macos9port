/* OS999 on folding phones: screens, postures, layout and the zoomable view.
 *
 * Everything here is SDL-free and deterministic so it runs in headless tests.
 * The host (sdl/mobile_main.c) feeds in the screen the platform reports and
 * draws the desktop where mobile_layout() says.
 */
#ifndef OS999_MOBILE_H
#define OS999_MOBILE_H

#include <stdint.h>
#include "ospi.h"

/* How the phone is being held. AUTO lets the platform decide. */
typedef enum {
    MP_AUTO = -1,
    MP_COVER = 0,   /* folded: the outer screen */
    MP_FLAT,        /* unfolded flat: the inner screen */
    MP_LAPTOP,      /* half folded, hinge horizontal: desktop up top, deck below */
    MP_BOOK,        /* half folded, hinge vertical: two pages */
    MP_POSTURE_COUNT
} MobilePosture;

typedef enum { MF_AXIS_NONE, MF_AXIS_HORIZONTAL, MF_AXIS_VERTICAL } MobileFoldAxis;
typedef enum { MF_STATE_NONE, MF_STATE_FLAT, MF_STATE_HALF } MobileFoldState;

typedef struct {
    int w, h;                   /* drawable pixels */
    float ppi;                  /* physical density, 0 when unknown */
    int inset_l, inset_t, inset_r, inset_b;   /* safe area in pixels */
    int inner;                  /* 1 = the large folding screen, -1 = unknown */
    MobileFoldAxis fold_axis;   /* orientation of the hinge line on screen */
    MobileFoldState fold_state;
    Rect fold;                  /* hinge bounds in pixels; may be zero-width */
    int hw_keyboard;            /* a Bluetooth/USB keyboard is attached */
    int pointer;                /* a trackpad or mouse is attached (iPad, Android tablets) */
    int tablet;                 /* a tablet: one large screen, no hinge */
} MobileScreen;

typedef struct {
    const char *id, *name;
    int cover_w, cover_h;  float cover_ppi;
    int inner_w, inner_h;  float inner_ppi;
    int flip;              /* clamshell: the cover is a small square */
    int rumored;           /* specs from leaks, not a shipping device */
    int tablet;            /* no cover screen, no hinge; inner_* is the screen, landscape */
} MobileDeviceProfile;

int mobile_device_count(void);
const MobileDeviceProfile *mobile_device_at(int index);
const MobileDeviceProfile *mobile_device_find(const char *id);
/* The screen a profile shows in a posture (simulator, shots and tests). */
void mobile_device_screen(const MobileDeviceProfile *d, MobilePosture p, MobileScreen *out);

MobilePosture mobile_posture_detect(const MobileScreen *s);
const char *mobile_posture_name(MobilePosture p);
const char *mobile_posture_hint(MobilePosture p);

enum { MOBILE_MAX_SCALES = 6, MOBILE_MIN_W = 400, MOBILE_MIN_H = 300 };

typedef struct {
    MobilePosture posture;
    float dp;                 /* pixels per 160-dpi point */
    int scale;                /* device pixels per desktop pixel */
    int desk_w, desk_h;       /* desktop resolution handed to session_paint */
    Rect desk;                /* where the desktop lands on screen at zoom 1 */
    Rect region;              /* area given to the desktop (desk sits centred) */
    Rect deck;                /* reserved for the laptop deck, else empty */
    Rect crease;              /* keep-clear band around the hinge, else empty */
    int kbd_posture;          /* KbdPosture for kbd_set_posture */
    int kbd_split;
    int kbd_always;           /* laptop: the deck is the keyboard; always shown */
    int tablet;
    int nscales, scales[MOBILE_MAX_SCALES], scale_index, default_scale;
} MobileLayout;

/* scale_pref 0 picks the size that gives classic-sized targets on this
 * screen; otherwise it asks for that many device pixels per desktop pixel. */
void mobile_layout(const MobileScreen *s, MobilePosture posture, int scale_pref,
                   MobileLayout *out);
/* Next/previous available desktop size, as Monitors would list them. */
int mobile_layout_cycle_scale(const MobileLayout *l, int direction);

/* Zoom and pan over the desktop, plus the lift that keeps the spot you were
 * touching above an on-screen keyboard. */
typedef struct {
    float zoom;          /* 1 .. MOBILE_MAX_ZOOM */
    float ox, oy;        /* desktop pixel at the top-left of the view */
    int lift;            /* screen pixels the desktop is raised by */
} MobileView;

#define MOBILE_MAX_ZOOM 4.0f

void mobile_view_reset(MobileView *v);
void mobile_view_clamp(MobileView *v, const MobileLayout *l);
void mobile_view_zoom_at(MobileView *v, const MobileLayout *l, float zoom, float sx, float sy);
void mobile_view_pan(MobileView *v, const MobileLayout *l, float dx_px, float dy_px);
/* Screen pixel -> desktop pixel (clamped to the desktop). Returns 1 when the
 * point was over the desktop. */
int mobile_view_to_desk(const MobileView *v, const MobileLayout *l, float sx, float sy,
                        int *dx, int *dy);
void mobile_view_to_screen(const MobileView *v, const MobileLayout *l, float dx, float dy,
                           float *sx, float *sy);
/* Source rectangle (desktop px) and destination (screen px) to present. */
void mobile_view_rects(const MobileView *v, const MobileLayout *l, Rect *src, Rect *dst);
/* Raise the desktop so desktop row focus_y stays above `occluded` screen rows. */
void mobile_view_lift(MobileView *v, const MobileLayout *l, int focus_y, int occluded,
                      int screen_h);

/* Keep windows where they were, proportionally, when the desktop resolution
 * changes (fold, unfold, Display size). Sizes shrink to fit and grow back. */
typedef struct {
    char rel[OSPI_MAX_WIN][OSPI_PATH];
    int want_w[OSPI_MAX_WIN], want_h[OSPI_MAX_WIN];
    int n;
} MobileContinuity;

void mobile_continuity_resize(MobileContinuity *c, Session *s, int new_w, int new_h);

#endif
