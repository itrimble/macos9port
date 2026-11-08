#ifndef MAC_WINDOW_INT_H
#define MAC_WINDOW_INT_H
#include "mac_runtime.h"
/* Native chrome geometry (procIDs 1 dBoxProc, 2 plainDBox, 4 noGrowDocProc, 12 zoomNoGrow).
 * Titled windows carry a 20px title bar; dBoxProc a 7px frame; the rest a 1px rule. */
static inline int mac_window_titled(int proc){return proc==4||proc==12;}
static inline int mac_window_frame(int proc){return proc==1?7:1;}
static inline int mac_window_top_extent(int proc){return mac_window_titled(proc)?20:mac_window_frame(proc);}
void mac_window_draw(MacRuntime *rt, int clear_content);
int mac_window_set_title(MacRuntime *rt, MacWindowState *w, uint32_t src); /* SetWTitle (mac_window_title.c) */
void mac_window_draw_clipped(MacRuntime *rt,const int16_t clip[4]);
int mac_window_hit(MacRuntime *rt, int x, int y);
int mac_window_registry_valid(MacRuntime *rt);
int mac_window_select(MacRuntime *rt,uint32_t address);
int mac_window_park(MacRuntime *rt);
void mac_window_publish(MacRuntime *rt,int front);
void mac_window_unlink(MacRuntime *rt,uint32_t address);
void mac_window_to_front(MacRuntime *rt,uint32_t address);
#endif
