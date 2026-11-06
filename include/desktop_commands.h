#ifndef OS999_DESKTOP_COMMANDS_H
#define OS999_DESKTOP_COMMANDS_H
#include "ospi.h"
/* Handles Get Info420, Select All421 and Copy422. No filesystem mutations. */
int desktop_command(Session *s, int command);
int desktop_command_enabled(Session *s, int command);
#endif
