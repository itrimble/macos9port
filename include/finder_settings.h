#ifndef OS999_FINDER_SETTINGS_H
#define OS999_FINDER_SETTINGS_H
#include "ospi.h"
#include "draw.h"
int finder_settings_command(Session *s, int command);
int finder_settings_key(Session *s, int key, int mods);
int finder_settings_down(Session *s, int x, int y);
int finder_settings_move(Session *s, int x, int y);
void finder_settings_paint(Fb *fb, const Session *s, Rect box);
uint32_t finder_label_color(int label);
#endif
