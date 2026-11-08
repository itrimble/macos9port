#ifndef MAC_DLGWINDOW_H
#define MAC_DLGWINDOW_H
#include "mac_dlg_int.h"
/* A dialog's colour port and control items (mac_dlgwindow.c). A dialog whose DITL has control items owns a HIDDEN registered window (proc 1) that covers the dialog
 * rectangle with the dialog's own coordinates as its local ones; it exists only to be the controls' owner and port (MacWindowState.port_only). */
uint32_t mac_dlgwin_create(MacRuntime *rt, int top, int left, int height, int width);   /* the window address; 0 after halting or on no memory */
void mac_dlgwin_shown(MacRuntime *rt, uint32_t window, int shown);   /* the dialog was shown or hidden: whether its controls draw */
void mac_dlgwin_destroy(MacRuntime *rt, uint32_t window);
/* Creates a control for every control item (the item's rectangle replaces the CNTL's); the guest-visible item handle is the control handle. The job `rt->cdef` must exist.
 * 0 ok, -1 halted. */
int mac_dlgwin_items(MacRuntime *rt, Dlg *d);
/* After a Dialog Manager call that completed: redraws the controls of a dialog whose native drawing just overwrote them (a guest call per custom control). 1 handled. */
int mac_dlgwin_flush(MacRuntime *rt);
#endif
