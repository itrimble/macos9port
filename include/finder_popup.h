#ifndef OS999_FINDER_POPUP_H
#define OS999_FINDER_POPUP_H
#include "ospi.h"
void finder_popup_set(Session *s, Window *w, int enabled);
void finder_popup_toggle(Session *s, Window *w);
void finder_popup_collapse_others(Session *s, int keep);
void finder_popup_layout(Session *s, Window *w);
#endif
