#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_color_port_service(MacRuntime *rt,uint16_t opcode);
#define CHECK(c) do { if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
#define SP 0x8000u
#define PORT 0x9000u
static uint32_t rd(MacRuntime*rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime*rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static MacRuntime fresh(void){
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x10000;rt.heap_limit=0x100000;rt.qd_globals=0x7000;
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,PORT);return rt;
}
static void lifecycle(void){
    MacRuntime rt=fresh();CHECK(mac_color_port_service(&rt,0xaa00)==1);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && rt.current_port==PORT);
    CHECK(rd(&rt,rt.qd_globals,4)==PORT && rd(&rt,PORT+6,2)==0xc000);
    uint32_t pmh=rd(&rt,PORT+2,4),pm=rd(&rt,pmh,4);
    CHECK(pmh!=rt.screen_pixmap && rd(&rt,pm,4)==rt.screen_base);
    CHECK(rd(&rt,pm+32,2)==8 && rd(&rt,pm+4,2)==0x8280);
    uint32_t cth=rd(&rt,pm+42,4),ct=rd(&rt,cth,4);
    CHECK(cth!=rt.screen_colors && rd(&rt,ct+4,2)==0 && rd(&rt,ct+6,2)==255);
    CHECK(rd(&rt,ct+8+255*8,2)==255);
    CHECK(rd(&rt,PORT+36,4)==0 && rd(&rt,PORT+42,4)==0xffffffff);
    CHECK(rd(&rt,PORT+80,4)==255 && rd(&rt,PORT+84,4)==0);
    CHECK(rd(&rt,PORT+52,4)==0x00010001 && rd(&rt,PORT+56,2)==8);
    uint32_t vis=rd(&rt,rd(&rt,PORT+24,4),4),clip=rd(&rt,rd(&rt,PORT+28,4),4);
    CHECK(rd(&rt,vis,2)==10 && rd(&rt,vis+6,2)==480 && rd(&rt,vis+8,2)==640);
    CHECK(rd(&rt,clip+2,2)==0x8001 && rd(&rt,clip+8,2)==0x7fff);
    uint32_t pen=rd(&rt,rd(&rt,PORT+58,4),4);
    CHECK(rd(&rt,pen,2)==0 && rd(&rt,pen+20,4)==0xffffffff);
    uint32_t heap=rt.heap_next;size_t count=rt.handle_count;
    wr(&rt,PORT+52,2,7);wr(&rt,vis+6,2,12);
    mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_color_port_service(&rt,0xaa01)==1);
    CHECK(rt.heap_next==heap && rt.handle_count==count && rd(&rt,PORT+2,4)==pmh);
    CHECK(rd(&rt,PORT+52,2)==1 && rd(&rt,vis+6,2)==480);
    mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_color_port_service(&rt,0xaa02)==1);
    CHECK(rd(&rt,pmh,4)==0 && rd(&rt,cth,4)==0 && rd(&rt,PORT+2,4)==0);
    CHECK(rd(&rt,rt.screen_pixmap,4)!=0 && rd(&rt,rt.screen_colors,4)!=0);
    CHECK(rd(&rt,PORT+6,2)==0 && !mac_cpu_halted(rt.cpu));
    CHECK(mac_color_port_service(&rt,0xafff)==0);
    mac_cpu_destroy(rt.cpu);
}
static void failures(void){
    MacRuntime rt=fresh();mac_cpu_set(rt.cpu,MAC_A7,0xfffffffe);
    CHECK(mac_color_port_service(&rt,0xaa00)==-1 && mac_cpu_halted(rt.cpu));
    CHECK(!rt.main_device && !rt.handle_count);mac_cpu_destroy(rt.cpu);
    rt=fresh();wr(&rt,SP,4,MAC_RUNTIME_RAM-106);
    CHECK(mac_color_port_service(&rt,0xaa00)==-1 && !rt.handle_count);mac_cpu_destroy(rt.cpu);
    rt=fresh();CHECK(mac_color_port_service(&rt,0xaa00)==1);
    uint32_t h=rd(&rt,PORT+2,4);wr(&rt,PORT+24,4,h); /* Not an owned region. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);
    CHECK(mac_color_port_service(&rt,0xaa02)==-1 && mac_cpu_halted(rt.cpu));
    CHECK(mac_cpu_ram(rt.cpu)[h]!=0 || mac_cpu_ram(rt.cpu)[h+1]!=0);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==SP);mac_cpu_destroy(rt.cpu);
}
int main(void){lifecycle();failures();puts("mac color port tests passed");return 0;}
