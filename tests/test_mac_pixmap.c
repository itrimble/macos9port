#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
int mac_pixmap_service(MacRuntime *,uint16_t);
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
static void init(MacRuntime *rt){memset(rt,0,sizeof(*rt));rt->cpu=mac_cpu_create(MAC_RUNTIME_RAM);rt->heap_next=0x100000;rt->heap_limit=0x200000;CHECK(mac_color_init(rt)==1);}
static uint32_t create(MacRuntime *rt){mac_cpu_set(rt->cpu,MAC_A7,0x8000);CHECK(mac_pixmap_service(rt,0xaa03)==1);CHECK(mac_cpu_get(rt->cpu,MAC_A7)==0x8000);return mac_cpu_read(rt->cpu,0x8000,4);}
static int drop(MacRuntime *rt,uint16_t op,uint32_t h){mac_cpu_set(rt->cpu,MAC_A7,0x8000);mac_cpu_write(rt->cpu,0x8000,4,h);return mac_pixmap_service(rt,op);}
static void life(void){
    MacRuntime rt;init(&rt);uint32_t before=rt.heap_next,h=create(&rt),p=mac_cpu_read(rt.cpu,h,4),cth=mac_cpu_read(rt.cpu,p+42,4),ct=mac_cpu_read(rt.cpu,cth,4);
    uint32_t screen=mac_cpu_read(rt.cpu,rt.screen_pixmap,4);
    CHECK(h && cth!=rt.screen_colors && rt.heap_next-before==68);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+p,mac_cpu_ram(rt.cpu)+screen,42));
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+p+46,mac_cpu_ram(rt.cpu)+screen+46,4));
    CHECK(mac_cpu_read(rt.cpu,ct+6,2)==0xffff);
    CHECK(mac_cpu_read(rt.cpu,p,4)==rt.screen_base);
    mac_cpu_ram(rt.cpu)[rt.screen_base]=0xa5;
    CHECK(drop(&rt,0xaa04,h)==1 && mac_cpu_get(rt.cpu,MAC_A7)==0x8004);
    CHECK(mac_cpu_read(rt.cpu,h,4)==0 && mac_cpu_read(rt.cpu,cth,4)==0);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base]==0xa5);
    CHECK(mac_cpu_read(rt.cpu,rt.screen_pixmap,4)==screen);
    size_t count=rt.handle_count;h=create(&rt);CHECK(rt.handle_count==count);p=mac_cpu_read(rt.cpu,h,4);cth=mac_cpu_read(rt.cpu,p+42,4);
    CHECK(drop(&rt,0xaa24,cth)==1);mac_cpu_write(rt.cpu,p+42,4,0);
    CHECK(drop(&rt,0xaa04,h)==1);
    mac_cpu_destroy(rt.cpu);
}
static void rejection(void){
    for(int kind=0;kind<6;kind++){
        MacRuntime rt;init(&rt);uint32_t h=create(&rt),p=mac_cpu_read(rt.cpu,h,4),cth=mac_cpu_read(rt.cpu,p+42,4);
        uint32_t table=mac_cpu_read(rt.cpu,cth,4),bad=h;
        if(kind==0)mac_cpu_write(rt.cpu,p+42,4,rt.screen_colors);
        if(kind==1)mac_cpu_write(rt.cpu,cth,4,MAC_RUNTIME_RAM-4);
        if(kind==2)bad=rt.screen_pixmap;
        if(kind==3)bad=h+1;
        if(kind==4)mac_cpu_write(rt.cpu,table+6,2,255);
        if(kind==5){uint32_t other=create(&rt),op=mac_cpu_read(rt.cpu,other,4);mac_cpu_write(rt.cpu,op+42,4,cth);}
        CHECK(drop(&rt,0xaa04,bad)==-1);
        CHECK(mac_cpu_ram(rt.cpu)[h+1]==(uint8_t)(p>>16));
        CHECK(rt.handles[3].handle==h && rt.handles[4].handle==cth);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt;init(&rt);uint32_t before=rt.heap_next;size_t count=rt.handle_count;
    rt.heap_limit=before+67;CHECK(create(&rt)==0);CHECK(rt.heap_next==before && rt.handle_count==count);
    CHECK((int16_t)mac_cpu_read(rt.cpu,0x220,2)==-108);mac_cpu_destroy(rt.cpu);
    init(&rt);mac_cpu_set(rt.cpu,MAC_A7,MAC_RUNTIME_RAM-2);before=rt.heap_next;
    CHECK(mac_pixmap_service(&rt,0xaa03)==-1 && rt.heap_next==before);mac_cpu_destroy(rt.cpu);
}
int main(void){life();rejection();if(!failures)puts("pixmap ownership checks passed");return !!failures;}
