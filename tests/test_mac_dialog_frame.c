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
static void frame(void){
 MacRuntime r=fresh();new_args(&r,STORAGE,0);wr(&r,SP+10,2,1);CHECK(mac_window_service(&r,0xaa45)==1);
 uint32_t structure=rd(&r,r.window.handles[8],4);CHECK(rd(&r,structure+2,2)==33 && rd(&r,structure+4,2)==43 && rd(&r,structure+6,2)==307 && rd(&r,structure+8,2)==457);
 CHECK(call(&r,0xa915,STORAGE)==1);uint8_t*p=mac_cpu_ram(r.cpu)+r.screen_base;
 CHECK(p[33*640+100]==255 && p[34*640+100]==0 && p[35*640+100]==255 && p[36*640+100]==255 && p[37*640+100]==0);
 CHECK(p[40*640+50]==0 && p[32*640+100]==77);
 CHECK(mac_window_hit(&r,100,100)==3 && mac_window_hit(&r,100,35)==0 && mac_window_hit(&r,44,100)==0);
 /* A clipped frame repair cannot change content or pixels outside the clip. */
 memset(p,77,307200);int16_t clip[4]={33,43,40,60};mac_window_draw_clipped(&r,clip);
 CHECK(p[33*640+43]==255 && p[33*640+60]==77 && p[40*640+50]==77);
 CHECK(call(&r,0xa80a,STORAGE)==1 && rd(&r,SP+4,2)==1);
 p[32*640+100]=42;CHECK(call(&r,0xa92d,STORAGE)==1);CHECK(p[33*640+43]==77 && p[32*640+100]==42);mac_cpu_destroy(r.cpu);
}
int main(void){frame();puts("mac dBoxProc frame tests passed");return 0;}
