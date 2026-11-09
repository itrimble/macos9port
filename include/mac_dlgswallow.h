#ifndef MAC_DLGSWALLOW_H
#define MAC_DLGSWALLOW_H
#include <stdint.h>
/* QuickDraw calls accepted and ignored while a dialog is the current port: argument bytes to pop, or -1. */
int mac_dlg_swallowed_args(uint16_t op);
int mac_dlg_drawing_trap(uint16_t op);
#endif
