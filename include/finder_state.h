#ifndef OS999_FINDER_STATE_H
#define OS999_FINDER_STATE_H
#include "ospi.h"
void finder_state_remember(Session *s, const Window *w, int open);
void finder_state_apply(Session *s, Window *w);
void finder_state_retarget(Session *s, const char *parent, const char *name, const char *destination, const char *next);
int finder_state_load(Session *s);
int finder_state_save(Session *s);
int finder_directory_exists(const char *root, const char *relative);
#endif
