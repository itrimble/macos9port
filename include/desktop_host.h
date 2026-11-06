#ifndef OS999_DESKTOP_HOST_H
#define OS999_DESKTOP_HOST_H

#include "ospi.h"

int desktop_host_resolve_volume(int argc, char **argv, char *out, size_t n);
void desktop_host_init(const char *volume);
void desktop_host_tick(Session *session, int guest_active);
void desktop_host_shutdown(void);

#endif
