#ifndef OS999_DISPLAY_MODES_H
#define OS999_DISPLAY_MODES_H

#include "ospi.h"

/* Supported logical desktop sizes (docs/DISPLAY-MODES.md). The table holds
 * DISPLAY_ASPECTS groups of DISPLAY_SIZES landscape sizes; every one also has
 * a portrait counterpart with width and height exchanged. A mode index is
 * (portrait * DISPLAY_ASPECTS + aspect) * DISPLAY_SIZES + size. */
enum { DISPLAY_ASPECTS = 3, DISPLAY_SIZES = 5,
       DISPLAY_MODE_COUNT = 2 * DISPLAY_ASPECTS * DISPLAY_SIZES };

int display_mode_index(int aspect, int size, int portrait);
int display_mode_size(int index, int *w, int *h);       /* 0, or -1 for a bad index */
const char *display_mode_aspect(int index);             /* "4:3", "10:16", ... */
const char *display_aspect_name(int aspect);            /* landscape name of a group */
int display_mode_find(int w, int h);                    /* index, or -1 when unsupported */

/* Make w x h the current desktop size (OSPI_W, OSPI_H). Only table sizes are
 * accepted; anything else leaves the size alone and returns -1. */
int display_set_size(int w, int h);

/* display_set_size, then bring the session into the new desktop: sw/sh, Finder
 * windows, desktop icons, the application switcher, panels and dialogs. The
 * host follows OSPI_W/OSPI_H for its buffers, texture and window. */
int display_mode_apply(Session *s, int w, int h);

/* The saved mode of a volume ("monitors" desktop_width/desktop_height and
 * window_scale in the control-panel store). Missing or unsupported sizes give
 * 1024 x 768; a missing or out-of-range scale gives 100 percent. */
void display_mode_saved(const char *volume, int *w, int *h, int *scale);
/* display_mode_saved + display_set_size: call before the first frame. Returns
 * the saved scale percent. */
int display_mode_startup(const char *volume);

/* Host window size for a w x h desktop shown at `scale` percent, held inside
 * area_w x area_h (0 means unlimited) with the desktop's shape kept. */
void display_window_fit(int w, int h, int scale, int area_w, int area_h, int *out_w, int *out_h);
/* A host-window point to desktop coordinates for a window of the given size. */
void display_point(int x, int y, int window_w, int window_h, int *out_x, int *out_y);

/* Keep a window of width w reachable: its title bar stays below the menu bar
 * and above the bottom edge, and `grip` pixels of its width stay on the desktop. */
void display_keep_reachable(int *x, int *y, int w, int grip);

#endif
