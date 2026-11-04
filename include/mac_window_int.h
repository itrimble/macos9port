#ifndef MAC_WINDOW_INT_H
#define MAC_WINDOW_INT_H
#include "mac_runtime.h"
void mac_window_draw(MacRuntime *rt, int clear_content);
void mac_window_draw_clipped(MacRuntime *rt,const int16_t clip[4]);
int mac_window_hit(MacRuntime *rt, int x, int y);
int mac_window_registry_valid(MacRuntime *rt);
int mac_window_select(MacRuntime *rt,uint32_t address);
int mac_window_park(MacRuntime *rt);
void mac_window_publish(MacRuntime *rt,int front);
void mac_window_unlink(MacRuntime *rt,uint32_t address);
void mac_window_to_front(MacRuntime *rt,uint32_t address);
#endif
