#ifndef OS999_APPEARANCE_PANEL_H
#define OS999_APPEARANCE_PANEL_H

#include "ospi.h"
#include "draw.h"

enum { APPEARANCE_DESKTOP_TAB, APPEARANCE_THEMES_TAB };
void appearance_panel_open_controls(Session *session);
void appearance_panel_open(Session *session, int tab);
int appearance_panel_active(void);
void appearance_panel_close(void);
void appearance_panel_draw(Session *session, Fb *fb);
/* Consume content clicks/keys, but leave the menu bar to normal input. */
int appearance_panel_down(Session *session, int x, int y);
int appearance_panel_context(Session *session, int x, int y);
int appearance_panel_key(Session *session, int key, int mods);

#endif
