#ifndef OS999_FINDER_LABELS_H
#define OS999_FINDER_LABELS_H
#include "ospi.h"
#include "draw.h"
int finder_labels_load(Session *s);
const char *finder_label_name(int label);
uint32_t finder_label_color(int label);
uint32_t finder_label_ink(int label);
int finder_labels_command(Session *s, int command);
void finder_labels_preferences(Session *s);
int finder_labels_cancel(Session *s);
int finder_labels_key(Session *s, int key, int mods);
int finder_labels_down(Session *s, int x, int y);
int finder_labels_text(Session *s, const char *text);
void finder_labels_paint(Fb *fb, const Session *s, Rect box);
#endif
