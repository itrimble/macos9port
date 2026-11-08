#ifndef OS999_CDEV_UI_H
#define OS999_CDEV_UI_H

#include "draw.h"
#include "ospi.h"

uint32_t cdev_ui_selection_color(void);
uint32_t cdev_ui_accent_text(void);
void cdev_ui_group(Fb *fb, Rect r, const char *title);
void cdev_ui_button(Fb *fb, Rect r, const char *label, int active, int enabled);
void cdev_ui_checkbox(Fb *fb, Rect r, const char *label, int checked);
void cdev_ui_slider(Fb *fb, Rect track, int value, int max_value, int muted);
void cdev_ui_icon(Fb *fb, int x, int y, const char *icon_name);
void cdev_ui_pattern_tile(Fb *fb, Rect r, int pattern_id, int selected);

#endif
