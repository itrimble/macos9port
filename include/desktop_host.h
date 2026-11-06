#ifndef OS999_DESKTOP_HOST_H
#define OS999_DESKTOP_HOST_H

#include "ospi.h"

void desktop_host_init(const char *volume);
void desktop_host_tick(Session *session, int guest_active);
void desktop_host_shutdown(void);

#endif
