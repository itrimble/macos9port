#ifndef OS999_MENU_NAVIGATION_H
#define OS999_MENU_NAVIGATION_H
#include "ospi.h"
/* SDL keycodes without making the headless desktop depend on SDL headers. */
enum { MENU_KEY_RIGHT = 1073741903, MENU_KEY_LEFT = 1073741904,
       MENU_KEY_DOWN = 1073741905, MENU_KEY_UP = 1073741906 };
/* Only handles an open menu. Command shortcuts dismiss it and return zero so
 * the caller can run its normal shortcut dispatch. Other keys are consumed. */
int menu_navigation_key(Session *s, int key, int mods);
#endif
