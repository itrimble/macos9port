#ifndef OS999_FINDER_RECENT_H
#define OS999_FINDER_RECENT_H
#include "ospi.h"
void finder_recent_retarget(Session *s, const char *old, const char *next);
void finder_recent_load(Session *s);
void finder_recent_note(Session *s, const char *path, int application);
int finder_recent_command(Session *s, int command);
int finder_recent_valid(const Session *s, const char *path);
#endif
