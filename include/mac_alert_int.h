#ifndef MAC_ALERT_INT_H
#define MAC_ALERT_INT_H
#include "mac_ditl.h"
#include "mac_runtime.h"
/* State of the native modal alert (mac_alert.c) while it is open. */
typedef struct MacAlertState {
    uint32_t sp, trap_pc;
    int16_t id;
    MacDitl ditl;
    int left, top, width, height;
    unsigned default_item;
    uint8_t *backdrop;
    int pressed, dirty, drawn;
} MacAlertState;
#endif
