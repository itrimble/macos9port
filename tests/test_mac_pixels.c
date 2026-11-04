#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
int mac_pixels_service(MacRuntime *,uint16_t);
static int failures;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;}}while(0)
static uint32_t setup(MacRuntime *rt,MacHandle **buffer) {
    memset(rt,0,sizeof(*rt));rt->cpu=mac_cpu_create(MAC_RUNTIME_RAM);
    rt->heap_next=0x100000;rt->heap_limit=0x300000;
    CHECK(mac_color_init(rt)==1);mac_cpu_set(rt->cpu,MAC_A7,0x8000);
    CHECK(mac_pixmap_service(rt,0xaa03)==1);
    uint32_t h=mac_cpu_read(rt->cpu,0x8000,4),p=mac_cpu_read(rt->cpu,h,4);
    mac_cpu_set(rt->cpu,MAC_D0,640*400+5120);CHECK(mac_memory_service(rt,0xa11e)==1);
    uint32_t base=mac_cpu_get(rt->cpu,MAC_A0);
    mac_cpu_write(rt->cpu,p,4,base);mac_cpu_write(rt->cpu,p+10,2,400);
    *buffer=&rt->handles[rt->handle_count-1];
    return h;
}
static int call(MacRuntime *rt,uint32_t selector,uint32_t h) {
    mac_cpu_set(rt->cpu,MAC_A7,0x8000);mac_cpu_set(rt->cpu,MAC_D0,selector);
    mac_cpu_write(rt->cpu,0x8000,4,h);mac_cpu_write(rt->cpu,0x8004,2,0xa55a);
    return mac_pixels_service(rt,0xab1d);
}
static void states(void) {
    MacRuntime rt;MacHandle *buffer;uint32_t h=setup(&rt,&buffer),base=buffer->pointer;
    uint32_t p=mac_cpu_read(rt.cpu,h,4);buffer->state=0x41;
    CHECK(call(&rt,0x0004000c,h)==1 && buffer->state==1);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==0x8004 && mac_cpu_read(rt.cpu,0x8004,2)==0xa55a);
    CHECK(call(&rt,0x00040001,h)==1 && buffer->state==0x81);
    CHECK(mac_cpu_read(rt.cpu,0x8004,2)==0x015a);
    CHECK(mac_cpu_read(rt.cpu,p,4)==base && buffer->pointer==base);
    CHECK(call(&rt,0x00040001,h)==1 && buffer->state==0x81); /* Idempotent lock. */
    CHECK(call(&rt,0x00040002,h)==1 && buffer->state==1);
    CHECK(call(&rt,0x00040002,h)==1 && buffer->state==1);
    CHECK(call(&rt,0x0004000f,h)==1 && buffer->state==1);
    CHECK(mac_cpu_read(rt.cpu,0x8004,4)==base && mac_cpu_get(rt.cpu,MAC_A7)==0x8004);
    CHECK(call(&rt,0x0004000b,h)==0 && buffer->state==1);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==0x8000 && !mac_cpu_halted(rt.cpu));
    CHECK(call(&rt,0x00080001,h)==0); /* Wrong selector argument size. */
    /* Bounds origins may be negative; only extents determine allocation span. */
    mac_cpu_write(rt.cpu,p+6,2,(uint16_t)-100);mac_cpu_write(rt.cpu,p+10,2,300);
    CHECK(call(&rt,0x00040001,h)==1);
    mac_cpu_destroy(rt.cpu);
}
static void malformed(void) {
    for(int kind=0;kind<10;kind++) {
        MacRuntime rt;MacHandle *buffer;uint32_t h=setup(&rt,&buffer);
        uint32_t p=mac_cpu_read(rt.cpu,h,4),base=buffer->pointer;buffer->state=0x41;
        if(kind==0)h++;
        if(kind==1)mac_cpu_write(rt.cpu,p,4,base+2); /* Interior pointer not owned root. */
        if(kind==2)buffer->size=640*400-1;
        if(kind==3)mac_cpu_write(rt.cpu,p+4,2,0x8001);
        if(kind==4)mac_cpu_write(rt.cpu,p+10,2,0);
        if(kind==5)mac_cpu_write(rt.cpu,p+32,2,16);
        if(kind==6)mac_cpu_write(rt.cpu,p,4,MAC_RUNTIME_RAM-2);
        if(kind==7)mac_cpu_write(rt.cpu,h,4,MAC_RUNTIME_RAM-48);
        if(kind==8)buffer->handle=0x7000; /* Handle-backed storage is not guessed. */
        if(kind==9)mac_cpu_write(rt.cpu,p+36,2,4);
        CHECK(call(&rt,kind%2?0x00040001:0x0004000f,h)==-1 && mac_cpu_halted(rt.cpu));
        CHECK(buffer->state==0x41 && buffer->pointer==base);
        CHECK(mac_cpu_get(rt.cpu,MAC_A7)==0x8000);
        CHECK(mac_cpu_ram(rt.cpu)[0x8004]==0xa5 && mac_cpu_ram(rt.cpu)[0x8005]==0x5a);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt;MacHandle *buffer;uint32_t h=setup(&rt,&buffer);buffer->state=0x40;
    mac_cpu_set(rt.cpu,MAC_A7,MAC_RUNTIME_RAM-4);mac_cpu_set(rt.cpu,MAC_D0,0x00040001);
    mac_cpu_write(rt.cpu,MAC_RUNTIME_RAM-4,4,h);
    CHECK(mac_pixels_service(&rt,0xab1d)==-1 && buffer->state==0x40);
    mac_cpu_destroy(rt.cpu);
    h=setup(&rt,&buffer);buffer->state=0x81;
    mac_cpu_set(rt.cpu,MAC_A7,MAC_RUNTIME_RAM-6);mac_cpu_set(rt.cpu,MAC_D0,0x0004000f);
    mac_cpu_write(rt.cpu,MAC_RUNTIME_RAM-6,4,h);mac_cpu_write(rt.cpu,MAC_RUNTIME_RAM-2,2,0x5a5a);
    CHECK(mac_pixels_service(&rt,0xab1d)==-1 && buffer->state==0x81);
    CHECK(mac_cpu_ram(rt.cpu)[MAC_RUNTIME_RAM-2]==0x5a && mac_cpu_ram(rt.cpu)[MAC_RUNTIME_RAM-1]==0x5a);
    mac_cpu_destroy(rt.cpu);
}
int main(void){states();malformed();if(!failures)puts("pointer-backed pixel-memory checks passed");return !!failures;}
