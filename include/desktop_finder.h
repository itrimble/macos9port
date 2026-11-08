#ifndef OS999_DESKTOP_FINDER_H
#define OS999_DESKTOP_FINDER_H
#include "ospi.h"
int finder_command(Session *s, int command);
int finder_command_enabled(const Session *s, int command);
#endif
