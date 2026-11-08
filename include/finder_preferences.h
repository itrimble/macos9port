#ifndef OS999_FINDER_PREFERENCES_H
#define OS999_FINDER_PREFERENCES_H
#include "ospi.h"
int finder_preferences_load(Session *s);
/* Commit only after an atomic save; enabled is boolean, delay is 100..2000 ms. */
int finder_preferences_save(Session *s, int enabled, int delay);
#endif
