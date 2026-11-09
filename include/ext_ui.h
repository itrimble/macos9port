#ifndef OS999_EXT_UI_H
#define OS999_EXT_UI_H

#include "draw.h"
#include "ext_registry.h"
#include "ospi.h"

/* Shared model and drawing for the Extensions Manager control panel and the
 * boot-time overlay shown when Space is held at startup. */
enum { EXT_UI_ROWS = 10, EXT_UI_ROW_H = 22 };

typedef struct {
    ExtList list;
    char root[OSPI_PATH];
    int scroll;     /* first visible row */
    int next_set;   /* "Use Set" cycles through saved sets */
    int overlay;    /* boot overlay: shows Continue buttons */
    int done;       /* overlay: user chose Continue */
    int disable_all_and_continue;
    char note[96];
} ExtManager;

/* Scan, load persisted state, order. Returns the extension count or -1. */
int ext_manager_open(ExtManager *m, const char *root, int overlay);
void ext_manager_render(ExtManager *m, Fb *fb, Rect content);
/* Mouse press in window coordinates; returns 1 when handled. */
int ext_manager_click(ExtManager *m, Rect content, int x, int y);
int ext_manager_key(ExtManager *m, int key);
int ext_manager_wheel(ExtManager *m, int dy);

Rect ext_ui_checkbox_rect(Rect content, int visible_row);
Rect ext_ui_button_rect(Rect content, int index); /* 0 Enable All, 1 Disable All, 2 Save Set, 3 Use Set, 4 Continue, 5 Disable All+Continue */

#endif
