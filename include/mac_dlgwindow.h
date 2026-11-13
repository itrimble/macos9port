#ifndef MAC_DLGWINDOW_H
#define MAC_DLGWINDOW_H
#include "mac_dlg_int.h"
/* A dialog's colour port and control items (mac_dlgwindow.c). A dialog whose DITL has control items owns a HIDDEN registered window (proc 1) that covers the dialog
 * rectangle with the dialog's own coordinates as its local ones; it exists only to be the controls' owner and port (MacWindowState.port_only). */
uint32_t mac_dlgwin_create(MacRuntime *rt, int top, int left, int height, int width);   /* the window address; 0 after halting or on no memory */
/* The dialog's port window, created on first need (SetPort on a dialog); 0 after halting. */
/* Whether the colour window machinery a port window needs is up (QuickDraw initialised with a main device) and the registry has room; when not, SetPort(dialog) keeps the old behaviour: the dialog's QuickDraw is swallowed. */
int mac_dlgwin_available(const MacRuntime *rt);
uint32_t mac_dlgwin_ensure(MacRuntime *rt, Dlg *d);
uint32_t mac_dlgwin_of_dialog(MacRuntime *rt, uint32_t dialog);   /* the port window of an open dialog named by its DialogPtr, created on first need; 0 when it is not an open dialog (or after halting) */
int mac_dlgwin_size(MacRuntime *rt, uint32_t window, int width, int height);   /* the dialog was resized: its port window follows; 1, or 0 after halting */
void mac_dlgwin_shown(MacRuntime *rt, uint32_t window, int shown);   /* the dialog was shown or hidden: whether its controls draw */
void mac_dlgwin_destroy(MacRuntime *rt, uint32_t window);
int mac_dlgwin_owe_redraw(MacRuntime *rt, uint32_t window);   /* defer a control redraw requested inside a guest call; 1 if the window is a dialog's */
int mac_dlgwin_update(MacRuntime *rt, uint32_t window, int begin);   /* BeginUpdate (begin) / EndUpdate on a dialog's port window: brackets the drawing; 1, or -1 after halting */
/* Creates a control for every control item (the item's rectangle replaces the CNTL's); the guest-visible item handle is the control handle. The job `rt->cdef` must exist.
 * 0 ok, -1 halted. */
int mac_dlgwin_items(MacRuntime *rt, Dlg *d);
/* After a Dialog Manager call that completed: redraws the controls of a dialog whose native drawing just overwrote them (a guest call per custom control). 1 handled. */
int mac_dlgwin_flush(MacRuntime *rt);
#endif
