#ifndef ABOUT_PANEL_H
#define ABOUT_PANEL_H
#include "draw.h"
#include "ospi.h"

/* About This Mac: banner, memory rows and per-application memory bars, drawn
 * from live session state. Returns 1 when w is the About window. */
int about_panel_paint(Fb *fb, const Session *s, const Window *w, const Layout *lay);
/* Resident memory of this process in bytes (0 when unavailable). */
uint64_t about_panel_resident_bytes(void);
#endif
