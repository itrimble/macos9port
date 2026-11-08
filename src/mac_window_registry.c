#include "mac_window_int.h"
#include <string.h>

/* Bounded owned window registry. Service selection is independent of the
 * front-to-back guest WindowList and never changes QuickDraw's current port.
 * Only one visible window is currently supported; hidden auxiliaries retain
 * their complete port, region, title, storage and lifecycle records. */
MacWindowState *mac_window_find(MacRuntime *rt,uint32_t address) {
    if(!address)return NULL;
    if(rt->window.address==address)return &rt->window;
    for(unsigned i=0;i<7;i++)if(rt->parked_windows[i].address==address)return &rt->parked_windows[i];
    return NULL;
}
uint32_t mac_window_front(MacRuntime *rt) {
    for(unsigned i=0;i<rt->window_count;i++) {
        MacWindowState *w=mac_window_find(rt,rt->window_order[i]);
        if(w && w->visible)return w->address;
    }
    return 0;
}
int mac_window_select(MacRuntime *rt,uint32_t address) {
    MacWindowState *w=mac_window_find(rt,address);
    if(!w)return 0;
    if(w!=&rt->window){MacWindowState saved=rt->window;rt->window=*w;*w=saved;}
    return 1;
}
int mac_window_park(MacRuntime *rt) {
    if(!rt->window.address)return 1;
    for(unsigned i=0;i<7;i++)if(!rt->parked_windows[i].address) {
        rt->parked_windows[i]=rt->window;memset(&rt->window,0,sizeof rt->window);return 1;
    }
    return 0;
}
static void links(MacRuntime *rt) {
    mac_cpu_write(rt->cpu,0x9d6,4,rt->window_count?rt->window_order[0]:0);
    for(unsigned i=0;i<rt->window_count;i++)
        mac_cpu_write(rt->cpu,rt->window_order[i]+144,4,i+1<rt->window_count?rt->window_order[i+1]:0);
}
/* Inserts the selected (new) window into the WindowList: in front (behind = -1), at the back (0), or directly behind the registered window `behind`. */
void mac_window_publish_behind(MacRuntime *rt,uint32_t behind) {
    unsigned at=behind==UINT32_MAX?0:rt->window_count;
    if(behind && behind!=UINT32_MAX){unsigned k=0;while(k<rt->window_count && rt->window_order[k]!=behind)k++;if(k<rt->window_count)at=k+1;}
    for(unsigned i=rt->window_count;i>at;i--)rt->window_order[i]=rt->window_order[i-1];
    rt->window_order[at]=rt->window.address;rt->window_count++;links(rt);
}
void mac_window_publish(MacRuntime *rt,int front) {
    unsigned at=front?0:rt->window_count;
    for(unsigned i=rt->window_count;i>at;i--)rt->window_order[i]=rt->window_order[i-1];
    rt->window_order[at]=rt->window.address;rt->window_count++;links(rt);
}
void mac_window_unlink(MacRuntime *rt,uint32_t address) {
    unsigned at=0;while(at<rt->window_count && rt->window_order[at]!=address)at++;
    if(at<rt->window_count){
        for(unsigned i=at+1;i<rt->window_count;i++)rt->window_order[i-1]=rt->window_order[i];
        rt->window_order[--rt->window_count]=0;links(rt);
    }
}
void mac_window_to_front(MacRuntime *rt,uint32_t address) {
    unsigned at=0;while(at<rt->window_count && rt->window_order[at]!=address)at++;
    if(at<rt->window_count){for(unsigned i=at;i>0;i--)rt->window_order[i]=rt->window_order[i-1];rt->window_order[0]=address;links(rt);}
}

int mac_window_registry_valid(MacRuntime *rt) {
    if(rt->window_count>8 || mac_cpu_read(rt->cpu,0x9d6,4)!=(rt->window_count?rt->window_order[0]:0))return 0;
    for(unsigned i=0;i<rt->window_count;i++) {
        uint32_t p=rt->window_order[i];
        if(!mac_window_find(rt,p) || (p&1) || p>mac_cpu_ram_size(rt->cpu)-156 ||
           mac_cpu_read(rt->cpu,p+144,4)!=(i+1<rt->window_count?rt->window_order[i+1]:0))return 0;
        for(unsigned j=0;j<i;j++)if(rt->window_order[j]==p)return 0;
    }
    return 1;
}
/* FindWindow's search: the visible windows from front to back, the first whose structure holds the point (the part code from mac_window_hit). That window is left
 * selected, so rt->window is the window FindWindow reports; with no hit the front window is selected again and 0 is returned. */
int mac_window_hit_test(MacRuntime *rt,int h,int v) {
    uint32_t front=mac_window_front(rt);
    for(unsigned i=0;i<rt->window_count;i++) {
        MacWindowState *w=mac_window_find(rt,rt->window_order[i]);
        if(!w || !w->visible)continue;
        uint32_t address=w->address;
        if(!mac_window_select(rt,address))continue;
        int hit=mac_window_hit(rt,h,v);
        if(hit)return hit;
    }
    if(front)mac_window_select(rt,front);
    return 0;
}
