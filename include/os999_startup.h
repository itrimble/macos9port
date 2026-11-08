#ifndef OS999_STARTUP_H
#define OS999_STARTUP_H

#include <stdint.h>
#include "draw.h"

typedef enum Os999StartupPhase {
    OS999_STARTUP_INACTIVE,
    OS999_STARTUP_SPIN,
    OS999_STARTUP_SETTLE,
    OS999_STARTUP_HOLD,
    OS999_STARTUP_REVEAL,
    OS999_STARTUP_DONE,
    OS999_STARTUP_ERROR
} Os999StartupPhase;

typedef struct Os999Startup {
    Os999StartupPhase phase;
    uint32_t phase_started;
    int reduced_motion;
    int ready;
    int failed;
    double progress;
} Os999Startup;

/* The intro starts only after normal desktop initialization has succeeded. */
void os999_startup_begin(Os999Startup *startup, uint32_t now, int reduced_motion);
void os999_startup_fail(Os999Startup *startup, uint32_t now);
/* Release the renderer scratch buffer on completion or interrupted shutdown. */
void os999_startup_release(void);
int os999_startup_pref_read(const char *root);
int os999_startup_pref_write(const char *root, int enabled);
int os999_startup_active(const Os999Startup *startup);
/* Exact status line shown below the boot progress bar. */
const char *os999_startup_status_text(void);
/* Boot progress in 0..1. It only reaches 1 when the desktop is really ready. */
double os999_startup_progress(const Os999Startup *startup, uint32_t now);
void os999_startup_frame(Os999Startup *startup, uint32_t now, int ready,
                         uint32_t *pixels, const uint32_t *desktop,
                         int width, int height);
void os999_draw_menu_apple(Fb *fb, int x, int y);

#endif
