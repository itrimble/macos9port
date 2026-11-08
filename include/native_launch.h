#ifndef NATIVE_LAUNCH_INCLUDED
#define NATIVE_LAUNCH_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "runtime_bridge.h"

/* 1: not an extracted classic application, 0: launched, -1: launch failure.
 * A launch is not gameplay acceptance. Early failures include a private log;
 * failures occurring after the bounded startup check remain in that log. */
int native_open(const char *root, const char *full, char *err, size_t n);
typedef struct {
    int (*read)(uint32_t *, size_t, int *, int *, uint64_t *);
    int (*push)(RuntimeBridgeInput);
    int (*state)(uint32_t *, char *, size_t);
    void (*close)(void);
    const char *title;
} NativeSurfaceBackend;
/* Attaches only when no guest is active; the caller retains backend lifetime. */
int native_surface_attach(const NativeSurfaceBackend *backend);
int native_surface_active(void);
int native_surface_command(int command);
int native_surface_read(uint32_t *pixels, size_t capacity, int *width, int *height,
                        uint64_t *sequence);
int native_surface_push(RuntimeBridgeInput input);
int native_surface_state(uint32_t *state, char *message, size_t message_size);
const char *native_surface_title(void);
void native_surface_close(void);

#endif
