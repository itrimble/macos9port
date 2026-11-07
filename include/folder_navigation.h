#ifndef OS999_FOLDER_NAVIGATION_H
#define OS999_FOLDER_NAVIGATION_H
#include "ospi.h"
#include "menu_navigation.h"
/* SDL key values, kept independent of SDL for headless testing. */
enum { FOLDER_KEY_HOME = 1073741898, FOLDER_KEY_END = 1073741901,
       FOLDER_KEY_PAGE_UP = 1073741899, FOLDER_KEY_PAGE_DOWN = 1073741902 };
/* Unmodified scrolling/selection keys in the focused front window. Folder
 * navigation also supports ASCII initial-letter cycling. Both return zero
 * for shortcuts, overlays and windows of the wrong kind. */
int text_navigation_key(Session *s, int key, int mods);
int folder_navigation_key(Session *s, int key, int mods);
#endif
