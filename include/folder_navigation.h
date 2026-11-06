#ifndef OS999_FOLDER_NAVIGATION_H
#define OS999_FOLDER_NAVIGATION_H
#include "ospi.h"
#include "menu_navigation.h"
/* SDL key values, kept independent of SDL for headless testing. */
enum { FOLDER_KEY_HOME = 1073741898, FOLDER_KEY_END = 1073741901 };
/* Unmodified arrows/Home/End and ASCII initial-letter cycling in the front
 * folder. Returns zero for shortcuts, overlays and non-folder windows. */
int folder_navigation_key(Session *s, int key, int mods);
#endif
