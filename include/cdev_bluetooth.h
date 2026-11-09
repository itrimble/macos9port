#ifndef OS999_CDEV_BLUETOOTH_H
#define OS999_CDEV_BLUETOOTH_H
/* The Bluetooth control panel reads the radio through this small source so the
 * panel itself links in the headless core. The desktop app registers the real
 * manager (desktop_bt_view.c); tests register a fake. No source means the panel
 * says Bluetooth is not part of this build. */
#include <stddef.h>

enum { CDEV_BT_IDLE = 0, CDEV_BT_SCANNING, CDEV_BT_CONNECTING, CDEV_BT_UP, CDEV_BT_FAILED };

typedef struct {
    int (*available)(void);              /* controller present and powered */
    const char *(*unavailable_reason)(void);
    int (*state)(void);                  /* CDEV_BT_* */
    int (*error)(void);                  /* negative BT_* code, 0 when none */
    int (*device_count)(void);
    int (*device)(int index, char *name, size_t name_n, char *addr, size_t addr_n);
    int (*scan)(int seconds);            /* 0 started */
    int (*connect)(int index);           /* 0 accepted */
    void (*disconnect)(void);
} CdevBtSource;

void cdev_bluetooth_set_source(const CdevBtSource *source);

#endif
