#include "mac_window_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define STORAGE 0x700000u
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static MacRuntime fresh(void) {
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x100000;rt.heap_limit=0x600000;rt.qd_globals=0x7000;rt.modifiers=0x80;
    CHECK(mac_color_init(&rt)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);
    CHECK(mac_toolbox_service(&rt,0xa912)==1);
    memset(mac_cpu_ram(rt.cpu)+rt.screen_base,77,307200);
    return rt;
}
static void new_args(MacRuntime *rt,uint32_t storage,int visible) {
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,0x12345678);wr(rt,SP+4,2,0x01aa);
    wr(rt,SP+6,4,0xffffffff);wr(rt,SP+10,2,12);wr(rt,SP+12,2,visible?0x0100:0x00ff);
    wr(rt,SP+14,4,0x4001);wr(rt,SP+18,4,0x4100);wr(rt,SP+22,4,storage);
    memcpy(mac_cpu_ram(rt->cpu)+0x4001,"\015Monkey Island",14);
    wr(rt,0x4100,2,40);wr(rt,0x4102,2,50);wr(rt,0x4104,2,300);wr(rt,0x4106,2,450);
}
static int call(MacRuntime *rt,uint16_t op,uint32_t w){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,w);return mac_window_service(rt,op);}
static void registry(void) {
 MacRuntime r=fresh();new_args(&r,STORAGE,0);wr(&r,SP+10,2,2);CHECK(mac_window_service(&r,0xaa45)==1);
 uint32_t first=STORAGE,first_title=r.window.handles[11];
 new_args(&r,STORAGE+0x100,1);CHECK(mac_window_service(&r,0xaa45)==1);uint32_t second=STORAGE+0x100;
 CHECK(r.window_count==2 && rd(&r,0x9d6,4)==second && rd(&r,second+144,4)==first && rd(&r,first+144,4)==0);
 CHECK(mac_window_front(&r)==second && mac_window_find(&r,first)->handles[11]==first_title);
 CHECK(call(&r,0xa920,first)==1 && rd(&r,0x9d6,4)==first && mac_window_front(&r)==second);
 CHECK(rd(&r,first+144,4)==second && rd(&r,second+144,4)==0);
 uint32_t current=r.current_port;CHECK(call(&r,0xa917,first)==1 && rd(&r,SP+4,4)==0x12345678);
 CHECK(r.current_port==current && rd(&r,r.qd_globals,4)==current);
 mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_window_service(&r,0xa924)==1 && rd(&r,SP,4)==second);
 r.mouse_x=100;r.mouse_y=100;wr(&r,SP,4,0x6000);mac_cpu_set(r.cpu,MAC_A7,SP);
 CHECK(mac_event_service(&r,0xa972)==1 && rd(&r,0x6000,2)==60 && rd(&r,0x6002,2)==50);
 MacEvent event;CHECK(mac_window_update_event(&r,1<<6,&event)==1 && event.message==second);
 /* Address a hidden auxiliary region while the visible window stays front. */
 uint32_t h=mac_window_find(&r,first)->handles[10];
 wr(&r,0x5000,4,0);wr(&r,0x5004,4,0x000a000a);wr(&r,SP,4,0x5000);wr(&r,SP+4,4,h);mac_cpu_set(r.cpu,MAC_A7,SP);
 CHECK(mac_regions_service(&r,0xa8df)==1 && rd(&r,rd(&r,h,4)+6,2)==10);
 CHECK(call(&r,0xa92d,first)==1 && r.window_count==1 && rd(&r,0x9d6,4)==second && rd(&r,second+144,4)==0);
 CHECK(r.current_port==second && mac_window_front(&r)==second && !mac_window_find(&r,first));
 CHECK(call(&r,0xa92d,second)==1 && r.window_count==0 && rd(&r,0x9d6,4)==0);mac_cpu_destroy(r.cpu);
}
static void overlap(void){
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 new_args(&r,STORAGE+0x100,0);CHECK(mac_window_service(&r,0xaa45)==1);
 CHECK(call(&r,0xa915,STORAGE+0x100)==1 && mac_window_front(&r)==STORAGE+0x100);   /* frontmost in the list: allowed, covering the first */
 CHECK(r.window_count==2 && r.window.visible && mac_window_find(&r,STORAGE)->visible);mac_cpu_destroy(r.cpu);
}
static void hidden_capacity(void){
 MacRuntime r=fresh();for(unsigned i=0;i<8;i++){new_args(&r,STORAGE+i*256,0);wr(&r,SP+10,2,2);CHECK(mac_window_service(&r,0xaa45)==1);}
 CHECK(r.window_count==8 && mac_window_front(&r)==0);
 uint32_t heap=r.heap_next;new_args(&r,STORAGE+8*256,0);CHECK(mac_window_service(&r,0xaa45)==-1 && r.heap_next==heap && r.window_count==8);mac_cpu_destroy(r.cpu);
}
/* FindWindow looks at every visible window, front to back, and reports (and selects) the one that is hit. */
static int find(MacRuntime *r,int h,int v,uint32_t *which){
 mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,4,0x4300);wr(r,SP+4,2,(uint16_t)v);wr(r,SP+6,2,(uint16_t)h);wr(r,0x4300,4,0xdead);wr(r,SP+8,2,0x5555);
 CHECK(mac_window_service(r,0xa92c)==1&&mac_cpu_get(r->cpu,MAC_A7)==SP+8);*which=rd(r,0x4300,4);return (int)rd(r,SP+8,2);
}
static void find_window(void) {
 MacRuntime r=fresh();uint32_t which;
 new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);                                   /* A: content (40,50)-(300,450) */
 new_args(&r,STORAGE+0x100,1);wr(&r,0x4100,2,100);wr(&r,0x4102,2,200);wr(&r,0x4104,2,200);wr(&r,0x4106,2,350);CHECK(mac_window_service(&r,0xaa45)==1);   /* B in front: (100,200)-(200,350) */
 CHECK(mac_window_front(&r)==STORAGE+0x100);
 CHECK(find(&r,250,150,&which)==3 && which==STORAGE+0x100);                                      /* inside B (and A): the front one */
 CHECK(find(&r,100,250,&which)==3 && which==STORAGE && r.window.address==STORAGE);               /* inside A only: the back window, selected */
 CHECK(find(&r,100,30,&which)==4 && which==STORAGE);                                             /* A's title bar */
 CHECK(find(&r,250,80,&which)==4 && which==STORAGE+0x100);                                       /* B's title bar (above its content) */
 CHECK(find(&r,500,400,&which)==0 && which==0 && r.window.address==STORAGE+0x100);               /* nothing: the front window is selected again */
 CHECK(find(&r,10,10,&which)==0 && which==0);
 wr(&r,0xbaa,2,20);CHECK(find(&r,250,10,&which)==1 && which==0);                                  /* the menu bar still wins */
 mac_cpu_destroy(r.cpu);
 /* A hidden window is never hit. */
 r=fresh();new_args(&r,STORAGE,0);CHECK(mac_window_service(&r,0xaa45)==1);
 new_args(&r,STORAGE+0x100,1);CHECK(mac_window_service(&r,0xaa45)==1);
 CHECK(find(&r,100,100,&which)==3 && which==STORAGE+0x100);mac_cpu_destroy(r.cpu);
}
int main(void){find_window();registry();overlap();hidden_capacity();puts("mac multi-window tests passed");return 0;}
