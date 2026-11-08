#ifndef OS999_DESKTOP_HOST_H
#define OS999_DESKTOP_HOST_H

#include "ospi.h"

int desktop_host_resolve_volume(int argc, char **argv, char *out, size_t n);
void desktop_host_init(const char *volume);
void desktop_host_tick(Session *session, int guest_active);
void desktop_host_shutdown(void);
/* Release session-owned audio players before their shared output service. */
void desktop_host_end_session(Session *session);

struct SDL_Window; struct SDL_Renderer; struct SDL_Texture;
void desktop_host_sync_present(Session *s, struct SDL_Renderer *ren, struct SDL_Texture *tex,
                               const char *volume, int *boot_enabled);
void desktop_trace_input(Session *s, struct SDL_Window *win, struct SDL_Renderer *ren,
                         int raw_x, int raw_y, int x, int y, int clicks);
#endif
