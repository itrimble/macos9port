/* noGrowDocProc (procID 4): the zoomNoGrow chrome without a zoom box. */
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
static void new_args(MacRuntime *rt,int proc) {
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,0x12345678);wr(rt,SP+4,2,0x0100);
    wr(rt,SP+6,4,0xffffffff);wr(rt,SP+10,2,(uint32_t)proc);wr(rt,SP+12,2,0x0100);
    wr(rt,SP+14,4,0x4001);wr(rt,SP+18,4,0x4100);wr(rt,SP+22,4,STORAGE);
    memcpy(mac_cpu_ram(rt->cpu)+0x4001,"\011Room Edit",10);
    wr(rt,0x4100,2,40);wr(rt,0x4102,2,50);wr(rt,0x4104,2,300);wr(rt,0x4106,2,450);
}
static uint8_t px(MacRuntime *rt,int x,int y){return mac_cpu_ram(rt->cpu)[rt->screen_base+(unsigned)y*640+(unsigned)x];}
static int16_t structure_top(MacRuntime *rt){return (int16_t)rd(rt,rd(rt,rt->window.handles[8],4)+2,2);}
int main(void) {
    MacRuntime a=fresh(),z=fresh();
    new_args(&a,4);CHECK(mac_window_service(&a,0xaa45)==1 && !mac_cpu_halted(a.cpu));
    new_args(&z,12);CHECK(mac_window_service(&z,0xaa45)==1);
    CHECK(a.window.proc_id==4 && z.window.proc_id==12);
    /* GetWVariant-style query returns the procID's low nibble. */
    mac_cpu_set(a.cpu,MAC_A7,SP);wr(&a,SP,4,STORAGE);CHECK(mac_window_service(&a,0xa80a)==1 && rd(&a,SP+4,2)==4);
    /* The title bar sits above the content exactly as for zoomNoGrow. */
    CHECK(structure_top(&a)==20 && structure_top(&z)==20);
    CHECK(rd(&a,STORAGE+112,1)==1 && rd(&z,STORAGE+112,1)==1); /* goAway flag kept */
    CHECK(rd(&a,STORAGE+113,1)==0 && rd(&z,STORAGE+113,1)==1); /* spareFlag = zoomable */
    /* Frame and title bar pixels agree; only the zoom box differs. */
    for(int y=18;y<44;y++)for(int x=48;x<452;x++) {
        int zoom_area=x>=435 && x<445 && y>=25 && y<35;
        if(!zoom_area && px(&a,x,y)!=px(&z,x,y)){fprintf(stderr,"pixel (%d,%d) differs\n",x,y);CHECK(0);}
    }
    CHECK(px(&a,435,25)!=px(&z,435,25));
    /* Close box present on both; zoom hit only on zoomNoGrow; both drag from the bar. */
    CHECK(mac_window_hit(&a,57,27)==6 && mac_window_hit(&z,57,27)==6);
    CHECK(mac_window_hit(&z,437,27)==8 && mac_window_hit(&a,437,27)==4);
    CHECK(mac_window_hit(&a,100,100)==3 && mac_window_hit(&a,100,27)==4 && mac_window_hit(&a,10,10)==0);
    mac_cpu_destroy(a.cpu);mac_cpu_destroy(z.cpu);
    /* Every other procID still halts with a named diagnostic. */
    /* The other standard definitions: documentProc and zoomDocProc have a grow part, the zoom forms a zoom box, altDBoxProc no title. */
    int standard[]={0,3,5,8};
    for(unsigned i=0;i<4;i++) {
        MacRuntime r=fresh();new_args(&r,standard[i]);
        CHECK(mac_window_service(&r,0xa913)==1 && !mac_cpu_halted(r.cpu) && r.window_count==1 && r.window.proc_id==standard[i]);
        int grow=standard[i]==0||standard[i]==8,b=r.window.bounds[2],x=r.window.bounds[3];
        CHECK(mac_window_hit(&r,x-3,b-3)==(grow?5:3) && mac_window_hit(&r,x-30,b-30)==3);
        CHECK(mac_cpu_ram(r.cpu)[r.window.address+113]==(standard[i]==8));
        mac_cpu_destroy(r.cpu);
    }
    int unsupported[]={6,7,9,15,4096,5000};
    for(unsigned i=0;i<sizeof unsupported/sizeof *unsupported;i++) {
        MacRuntime r=fresh();new_args(&r,unsupported[i]);
        CHECK(mac_window_service(&r,0xaa45)<0 && mac_cpu_halted(r.cpu) && r.window_count==0);
        mac_cpu_destroy(r.cpu);
    }
    /* A procID that names the program's own window definition (id*16+variant, ids 1-255) is shown with titled chrome instead of being run. */
    int custom[]={16,160,176,2064,4095};
    for(unsigned i=0;i<sizeof custom/sizeof *custom;i++) {
        MacRuntime r=fresh();new_args(&r,custom[i]);
        CHECK(mac_window_service(&r,0xaa45)==1 && !mac_cpu_halted(r.cpu) && r.window.proc_id==4 && r.window_count==1);
        mac_cpu_destroy(r.cpu);
    }
    puts("noGrowDocProc chrome checks passed");return 0;
}
