#ifndef OS999_APP_SWITCHER_H
#define OS999_APP_SWITCHER_H
#include "ospi.h"
struct Fb;
int app_switcher_bounds(const Session *s, Rect *r);
int app_switcher_hit(const Session *s, int x, int y);
/* Running application identity at a row; zero for chrome or a modal desktop. */
int app_switcher_application_at(const Session *s, int x, int y);
void app_switcher_open(Session *s);
int app_switcher_down(Session *s, int x, int y);
int app_switcher_move(Session *s, int x, int y);
int app_switcher_up(Session *s, int x, int y);
int app_switcher_tear(Session *s, int x, int y);
void app_switcher_paint(const Session *s, struct Fb *fb);
int app_switcher_load(Session *s);
int app_switcher_save(const Session *s);
#endif
