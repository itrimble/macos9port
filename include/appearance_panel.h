#ifndef OS999_APPEARANCE_PANEL_H
#define OS999_APPEARANCE_PANEL_H

#include "ospi.h"
#include "draw.h"

enum { APPEARANCE_DESKTOP_TAB, APPEARANCE_THEMES_TAB };

typedef struct {
    int (*active)(void);
    int (*can_open)(void);
    int (*regions)(const Session *session, Rect out[2]);
    void (*close)(void);
    void (*deactivate)(void);
    void (*blur)(void);
    void (*draw)(Session *session, Fb *fb);
    int (*down)(Session *session, int x, int y, int clicks);
    int (*context)(Session *session, int x, int y);
    int (*key)(Session *session, int key, int mods);
} AppearancePanelDelegate;

void appearance_panel_set_delegate(const AppearancePanelDelegate *delegate);
void appearance_panel_close_local(void);
void appearance_panel_open_controls(Session *session);
void appearance_panel_open(Session *session, int tab);
int appearance_panel_active(void);
/* At most two rectangles: panel and its optional contextual menu. */
int appearance_panel_regions(const Session *session, Rect out[2]);
int appearance_panel_can_open(void);
void appearance_panel_close(void);
void appearance_panel_deactivate(void);
void appearance_panel_blur(void);
void appearance_panel_draw(Session *session, Fb *fb);
/* Consume content clicks/keys, but leave the menu bar to normal input. */
int appearance_panel_down(Session *session, int x, int y);
int appearance_panel_context(Session *session, int x, int y);
int appearance_panel_key(Session *session, int key, int mods);

#endif
