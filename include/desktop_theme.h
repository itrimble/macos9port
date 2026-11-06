#ifndef OS999_DESKTOP_THEME_H
#define OS999_DESKTOP_THEME_H
#include <stdint.h>
enum {
    THEME_PANEL, THEME_MENU, THEME_FOREGROUND, THEME_DISABLED, THEME_FRAME,
    THEME_TRACK, THEME_STRIPE_A, THEME_STRIPE_B, THEME_ACCENT,
    THEME_DESKTOP_TOP, THEME_DESKTOP_BOTTOM, THEME_ROLE_COUNT
};
/* Fresh built-in color palettes, not the missing external theme implementation.
 * NULL preference path selects memory-only operation. Missing/malformed content
 * uses Platinum. Invalid paths/I/O failures return -1 with Platinum selected. */
int desktop_theme_init(const char *preference_path);
int desktop_theme_count(void);
const char *desktop_theme_name(int index);
int desktop_theme_current(void);
/* 0 success, -1 invalid selection/save failure; failure retains current theme. */
int desktop_theme_select(int index);
int desktop_theme_next(void);
void desktop_theme_shutdown(void);
uint32_t desktop_theme_color(int role);
#endif
