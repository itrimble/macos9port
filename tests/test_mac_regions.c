#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_regions_service(MacRuntime *,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define R 0x4000u
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static MacRuntime fresh(void){MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);rt.heap_next=0x100000;rt.heap_limit=0x200000;mac_cpu_set(rt.cpu,MAC_A7,SP);return rt;}
static uint32_t create(MacRuntime *rt){mac_cpu_set(rt->cpu,MAC_A7,SP);CHECK(mac_regions_service(rt,0xa8d8)==1);CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP);return rd(rt,SP,4);}
static int replace(MacRuntime *rt,uint32_t h,uint32_t r){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,r);wr(rt,SP+4,4,h);return mac_regions_service(rt,0xa8df);}
static void lifecycle(void){
    MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4);
    CHECK(h && rd(&rt,p,2)==10 && rd(&rt,p+2,4)==0 && rd(&rt,p+6,4)==0);
    wr(&rt,R,2,(uint16_t)-20);wr(&rt,R+2,2,(uint16_t)-10);wr(&rt,R+4,2,30);wr(&rt,R+6,2,40);
    CHECK(replace(&rt,h,R)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+8);
    CHECK(!memcmp(mac_cpu_ram(rt.cpu)+p+2,mac_cpu_ram(rt.cpu)+R,8));
    CHECK(replace(&rt,h,p+2)==1); /* Overlapping source. */
    wr(&rt,R+4,2,(uint16_t)-20);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+2,4)==0 && rd(&rt,p+6,4)==0);
    wr(&rt,R+4,2,30);wr(&rt,R+6,2,(uint16_t)-11);CHECK(replace(&rt,h,R)==1 && rd(&rt,p+6,4)==0);
    rt.handles[0].state=0x80;wr(&rt,R+6,2,40);CHECK(replace(&rt,h,R)==1 && rd(&rt,h,4)==p);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,h);CHECK(mac_regions_service(&rt,0xa8d9)==1 && rd(&rt,h,4)==0);
    CHECK(!rt.handles[0].handle && create(&rt)!=0 && rt.handle_count==1);
    CHECK(mac_regions_service(&rt,0xa8e4)==0);mac_cpu_destroy(rt.cpu);
}
static void failures(void){
    for(int kind=0;kind<8;kind++) {
        MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4),r=R;
        if(kind==0)h++;
        if(kind==1)r=MAC_RUNTIME_RAM-6;
        if(kind==2)rt.handles[0].resource=1;
        if(kind==3)rt.handles[0].loaded_code=1;
        if(kind==4)wr(&rt,h,4,p+4);
        if(kind==5){rt.handles[0].size=8;rt.handles[0].state=0x80;}
        if(kind==6){rt.handles[0].size=8;rt.heap_high=rt.heap_next+8;}
        if(kind==7){ /* A nine-character window title is also a 10-byte handle. */
            rt.handles[0].type=0x4f57494e;rt.window.address=0x9000;rt.window.handles[11]=h;
        }
        uint8_t before[12];memcpy(before,mac_cpu_ram(rt.cpu)+p,12);uint32_t heap=rt.heap_next;
        CHECK(replace(&rt,h,r)==-1 && mac_cpu_halted(rt.cpu));
        CHECK(!memcmp(before,mac_cpu_ram(rt.cpu)+p,12) && rt.heap_next==heap && mac_cpu_get(rt.cpu,MAC_A7)==SP);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt=fresh();uint32_t h=create(&rt),p=rd(&rt,h,4);rt.handles[0].size=8;
    wr(&rt,R,4,0);wr(&rt,R+4,4,0x000a0014);
    CHECK(replace(&rt,h,R)==1 && rd(&rt,h,4)!=p && rt.handles[0].size==10);
    CHECK(rd(&rt,rd(&rt,h,4)+6,4)==0x000a0014);mac_cpu_destroy(rt.cpu);
    rt=fresh();rt.heap_high=rt.heap_next+15;uint32_t heap=rt.heap_next;
    CHECK(create(&rt)==0 && rt.heap_next==heap && rt.handle_count==0 && (int16_t)rd(&rt,0x220,2)==-108);
    mac_cpu_destroy(rt.cpu);
    rt=fresh();mac_cpu_set(rt.cpu,MAC_A7,MAC_RUNTIME_RAM-6);
    CHECK(mac_regions_service(&rt,0xa8df)==-1);mac_cpu_destroy(rt.cpu);
}
int main(void){lifecycle();failures();puts("rectangular region ownership checks passed");}
