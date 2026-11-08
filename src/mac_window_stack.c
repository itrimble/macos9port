#include "mac_window_int.h"
#include <string.h>

/* Visible windows share one screen. Each visible window keeps a snapshot of the screen taken when it was shown (its backdrop). That works as a
 * stack: a window may be shown only when it is first in the WindowList, so it covers what is visible and its backdrop holds exactly that.
 * Hiding or closing the frontmost visible window restores its backdrop. A window below the front can be hidden or closed only when the frontmost
 * one covers all of its frame (so no pixel of the screen changes); the covered window's backdrop, which is what lay behind it, is then copied
 * into the front window's backdrop over its frame, so that hiding the front window later shows what is really behind. A partly uncovered window
 * would need real clipping and compositing and is refused. Returns 1 if the change is allowed (and the backdrop adjusted), else 0. */
static void frame_of(const MacWindowState *w,int out[4]) {
    int f=mac_window_frame(w->proc_id);
    out[0]=w->bounds[0]-mac_window_top_extent(w->proc_id);out[1]=w->bounds[1]-f;out[2]=w->bounds[2]+f;out[3]=w->bounds[3]+f;
}
int mac_window_stack_ok(MacRuntime *rt,uint32_t address,int show) {
    if(show)return rt->window_count && rt->window_order[0]==address;
    uint32_t front=mac_window_front(rt);
    if(front==address)return 1;
    MacWindowState *w=mac_window_find(rt,address),*t=mac_window_find(rt,front);
    if(!w || !t || !w->visible)return 0;
    int a[4],c[4];frame_of(w,a);frame_of(t,c);
    if(a[0]<c[0] || a[1]<c[1] || a[2]>c[2] || a[3]>c[3])return 0;               /* not wholly under the front window */
    if(a[0]<0)a[0]=0;if(a[1]<0)a[1]=0;if(a[2]>480)a[2]=480;if(a[3]>640)a[3]=640;
    if(a[0]>=a[2] || a[1]>=a[3])return 1;
    if(w->backdrop>mac_cpu_ram_size(rt->cpu) || mac_cpu_ram_size(rt->cpu)-w->backdrop<307200u ||
       t->backdrop>mac_cpu_ram_size(rt->cpu) || mac_cpu_ram_size(rt->cpu)-t->backdrop<307200u)return 0;
    uint8_t *ram=mac_cpu_ram(rt->cpu);
    for(int y=a[0];y<a[2];y++)memcpy(ram+t->backdrop+y*640+a[1],ram+w->backdrop+y*640+a[1],(size_t)(a[3]-a[1]));
    return 1;
}
