#ifndef MAC_WINDOW_INT_H
#define MAC_WINDOW_INT_H
#include "mac_runtime.h"
/* Native chrome geometry for the standard window definition IDs: 0 documentProc, 1 dBoxProc, 2 plainDBox, 3 altDBoxProc, 4 noGrowDocProc, 5 movableDBoxProc,
 * 8 zoomDocProc, 12 zoomNoGrow. Titled windows carry a 20px title bar; dBoxProc a 7px frame; the rest a 1px rule. Differences from the real definitions:
 * altDBoxProc has no drop shadow and movableDBoxProc is drawn as a titled document window without a close box rule of its own; documentProc and
 * zoomDocProc have a 15 x 15 grow part at the content's bottom right (FindWindow reports inGrow there). */
static inline int mac_window_std_proc(int proc){return proc==0||proc==1||proc==2||proc==3||proc==4||proc==5||proc==8||proc==12;}
static inline int mac_window_titled(int proc){return proc==0||proc==4||proc==5||proc==8||proc==12;}
static inline int mac_window_zoom(int proc){return proc==8||proc==12;}
static inline int mac_window_grow(int proc){return proc==0||proc==8;}
static inline int mac_window_frame(int proc){return proc==1?7:1;}
static inline int mac_window_top_extent(int proc){return mac_window_titled(proc)?20:mac_window_frame(proc);}
void mac_window_draw(MacRuntime *rt, int clear_content);
/* A procID that names a program's own window definition (WDEF id = procID / 16, variant = procID % 16, ids 1 to 255) is not run: it is shown with this runtime's
 * titled-window chrome (proc 4, no grow box) and *proc becomes 4. Returns 1 if it was such a procID. */
int mac_window_custom_wdef(int *proc);
void mac_window_publish_behind(MacRuntime *rt,uint32_t behind); /* mac_window_registry.c */
void mac_window_hilite(MacRuntime *rt, MacWindowState *w, int on); /* HiliteWindow (mac_window_title.c) */
int mac_window_set_title(MacRuntime *rt, MacWindowState *w, uint32_t src); /* SetWTitle (mac_window_title.c) */
void mac_window_draw_clipped(MacRuntime *rt,const int16_t clip[4]);
int mac_window_hit(MacRuntime *rt, int x, int y);
int mac_window_hit_test(MacRuntime *rt, int h, int v);   /* FindWindow over all visible windows, front to back; leaves the hit window selected */
int mac_window_registry_valid(MacRuntime *rt);
int mac_window_select(MacRuntime *rt,uint32_t address);
int mac_window_park(MacRuntime *rt);
void mac_window_publish(MacRuntime *rt,int front);
void mac_window_unlink(MacRuntime *rt,uint32_t address);
void mac_window_to_front(MacRuntime *rt,uint32_t address);
#endif
