#ifndef OS999_DESKTOP_BT_VIEW_H
#define OS999_DESKTOP_BT_VIEW_H
/* Connects the Bluetooth control panel to the desktop's manager. */
#ifdef OS999_DESKTOP_BLUETOOTH
void desktop_bt_view_register(void);
void desktop_bt_view_unregister(void);
#else
static inline void desktop_bt_view_register(void) {}
static inline void desktop_bt_view_unregister(void) {}
#endif
#endif
