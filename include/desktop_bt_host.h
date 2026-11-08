#ifndef OS999_DESKTOP_BT_HOST_H
#define OS999_DESKTOP_BT_HOST_H
/* Desktop-only service ownership. Mobile keeps the existing unavailable path. */
#ifdef OS999_DESKTOP_BLUETOOTH
void desktop_bt_host_init(void);
void desktop_bt_host_tick(void);
void desktop_bt_host_shutdown(void);
#else
static inline void desktop_bt_host_init(void) {}
static inline void desktop_bt_host_tick(void) {}
static inline void desktop_bt_host_shutdown(void) {}
#endif
#endif
