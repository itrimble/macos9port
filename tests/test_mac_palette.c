#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
int mac_palette_service(MacRuntime *,uint16_t);
static int failures;
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);failures++;} }while(0)
static void init(MacRuntime *rt) {
    memset(rt,0,sizeof(*rt));rt->cpu=mac_cpu_create(MAC_RUNTIME_RAM);
    rt->heap_next=0x100000;rt->heap_limit=0x200000;
    CHECK(mac_color_init(rt)==1);
}
static int entries(MacRuntime *rt,int start,int count,uint32_t p) {
    mac_cpu_set(rt->cpu,MAC_A7,0x8000);
    mac_cpu_write(rt->cpu,0x8000,4,p);mac_cpu_write(rt->cpu,0x8004,2,(uint16_t)count);
    mac_cpu_write(rt->cpu,0x8006,2,(uint16_t)start);
    return mac_palette_service(rt,0xaa3f);
}
static void spec(MacRuntime *rt,uint32_t p,unsigned index,unsigned r,unsigned g,unsigned b) {
    mac_cpu_write(rt->cpu,p,2,index);mac_cpu_write(rt->cpu,p+2,2,r);
    mac_cpu_write(rt->cpu,p+4,2,g);mac_cpu_write(rt->cpu,p+6,2,b);
}
static uint32_t get(MacRuntime *rt,int id) {
    mac_cpu_set(rt->cpu,MAC_A7,0x8000);mac_cpu_write(rt->cpu,0x8000,2,(uint16_t)id);
    CHECK(mac_palette_service(rt,0xaa18)==1);CHECK(mac_cpu_get(rt->cpu,MAC_A7)==0x8002);
    return mac_cpu_read(rt->cpu,0x8002,4);
}
static void successful(void) {
    MacRuntime rt;init(&rt);
    uint32_t ct=mac_cpu_read(rt.cpu,rt.screen_colors,4),seed=mac_cpu_read(rt.cpu,ct,4);
    spec(&rt,0x4000,65535,0x1234,0x5678,0x9abc); /* sequence ignores value */
    spec(&rt,0x4008,300,0xffff,0x8000,0);
    CHECK(entries(&rt,254,1,0x4000)==1);
    CHECK(rt.palette[254]==0x12569a && rt.palette[255]==0xff8000);
    CHECK(mac_cpu_read(rt.cpu,ct+8+254*8+2,2)==0x1234);
    CHECK(mac_cpu_read(rt.cpu,ct,4)!=seed && mac_cpu_get(rt.cpu,MAC_A7)==0x8008);
    spec(&rt,0x4000,7,0,0xffff,0);spec(&rt,0x4008,1,0xffff,0,0xffff);
    CHECK(entries(&rt,-1,1,0x4000)==1);
    CHECK(rt.palette[7]==0x00ff00 && rt.palette[1]==0xff00ff);
    /* Snapshot overlap: destination first write would otherwise replace next source. */
    spec(&rt,ct+8,2,0x1111,0x2222,0x3333);spec(&rt,ct+16,0,0xaaaa,0xbbbb,0xcccc);
    /* Aliased source value 2 also reserves device entry zero for client 2. */
    mac_cpu_write(rt.cpu,mac_cpu_read(rt.cpu,rt.main_device,4)+2,2,2);
    CHECK(entries(&rt,-1,1,ct+8)==1);
    CHECK(rt.palette[2]==0x112233 && rt.palette[0]==0xaabbcc);
    uint32_t h=get(&rt,8),h2=get(&rt,8),p=mac_cpu_read(rt.cpu,h,4);
    CHECK(h && h2 && h!=h2 && !rt.handles[rt.handle_count-1].resource);
    CHECK(mac_cpu_read(rt.cpu,p+4,2)==0 && mac_cpu_read(rt.cpu,p+6,2)==255);
    CHECK(mac_cpu_read(rt.cpu,p+10,2)==0xffff && mac_cpu_read(rt.cpu,p+2054,2)==0);
    CHECK(get(&rt,9)==0 && (int16_t)mac_cpu_read(rt.cpu,0xa60,2)==-192);
    mac_cpu_destroy(rt.cpu);
}
static void rejected(void) {
    for(int kind=0;kind<7;kind++) {
        MacRuntime rt;init(&rt);uint32_t ct=mac_cpu_read(rt.cpu,rt.screen_colors,4);
        spec(&rt,0x4000,1,0xffff,0,0);spec(&rt,0x4008,256,0xffff,0,0);
        int start=-1,count=1;uint32_t src=0x4000;
        if(kind==1){start=255;count=1;}
        if(kind==2){count=-1;}
        if(kind==3){src=MAC_RUNTIME_RAM-8;}
        if(kind==4){src=0x4001;}
        if(kind==5){mac_cpu_write(rt.cpu,ct+8+1*8,2,0x8000);count=0;}
        if(kind==6){mac_cpu_write(rt.cpu,rt.screen_colors,4,MAC_RUNTIME_RAM-8);}
        uint32_t saved[256];uint8_t table[2056];memcpy(saved,rt.palette,sizeof saved);
        memcpy(table,mac_cpu_ram(rt.cpu)+ct,sizeof table);
        CHECK(entries(&rt,start,count,src)==-1);
        CHECK(!memcmp(saved,rt.palette,sizeof saved));CHECK(!memcmp(table,mac_cpu_ram(rt.cpu)+ct,sizeof table));
        CHECK(mac_cpu_get(rt.cpu,MAC_A7)==0x8000);mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt;init(&rt);uint32_t saved=rt.heap_next;size_t handles=rt.handle_count;
    rt.heap_limit=saved+4;CHECK(get(&rt,8)==0);CHECK(rt.heap_next==saved && rt.handle_count==handles);
    CHECK((int16_t)mac_cpu_read(rt.cpu,0x220,2)==-108);mac_cpu_destroy(rt.cpu);
}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){put16(p,v>>16);put16(p+2,v);}
static void guest_resource(void) {
    /* One original synthetic 1-entry clut; no proprietary resource bytes. */
    uint8_t bytes[434]={0};
    put32(bytes,256);put32(bytes+4,384);put32(bytes+8,20);put32(bytes+12,50);
    put32(bytes+256,16);put32(bytes+260,1234);put16(bytes+266,0);
    put16(bytes+268,9);put16(bytes+270,0x1234);put16(bytes+272,0x5678);put16(bytes+274,0x9abc);
    put16(bytes+408,28);put16(bytes+410,50);put16(bytes+412,0);
    put32(bytes+414,MAC_RESOURCE_TYPE('c','l','u','t'));
    put16(bytes+418,0);put16(bytes+420,10);put16(bytes+422,8);put16(bytes+424,0xffff);
    MacRuntime rt;init(&rt);char err[128];
    CHECK(mac_resource_open(&rt.fork,bytes,sizeof bytes,err,sizeof err)==0);
    uint32_t h=get(&rt,8),p=mac_cpu_read(rt.cpu,h,4);
    CHECK(h && mac_cpu_read(rt.cpu,p,4)==1234 && mac_cpu_read(rt.cpu,p+6,2)==0);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+p,bytes+260,16));
    CHECK(!rt.handles[rt.handle_count-1].resource);
    mac_cpu_write(rt.cpu,p+10,2,0xffff);
    uint32_t h2=get(&rt,8),p2=mac_cpu_read(rt.cpu,h2,4);
    CHECK(h2!=h && mac_cpu_read(rt.cpu,p2+10,2)==0x1234);
    uint32_t saved=rt.heap_next;size_t count=rt.handle_count;
    put16(bytes+266,255); /* Declares more entries than the resource contains. */
    mac_cpu_set(rt.cpu,MAC_A7,0x8000);mac_cpu_write(rt.cpu,0x8000,2,8);
    CHECK(mac_palette_service(&rt,0xaa18)==-1);
    CHECK(rt.heap_next==saved && rt.handle_count==count);
    mac_cpu_destroy(rt.cpu);
}
int main(void){successful();rejected();guest_resource();if(!failures)puts("palette checks passed");return !!failures;}
