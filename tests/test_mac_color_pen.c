#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_color_pen_service(MacRuntime*,uint16_t);
#define CHECK(c) do { if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
#define SP 0x8000u
#define PORT 0x9000u
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static MacRuntime fresh(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    r.heap_next=0x10000;r.heap_limit=0x100000;r.qd_globals=0x7000;
    wr(&r,SP,4,PORT);mac_cpu_set(r.cpu,MAC_A7,SP);
    CHECK(mac_color_port_service(&r,0xaa00)==1);return r;
}
static int call(MacRuntime*r,uint16_t op){mac_cpu_set(r->cpu,MAC_A7,SP);return mac_color_pen_service(r,op);}
static void patterns(void){
    MacRuntime r=fresh();uint32_t pen=rd(&r,rd(&r,PORT+58,4),4),back=rd(&r,rd(&r,PORT+32,4),4);
    uint8_t bits[8]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
    memcpy(mac_cpu_ram(r.cpu)+0xa001,bits,8);wr(&r,SP,4,0xa001);
    CHECK(call(&r,0xa89d)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    CHECK(!memcmp(mac_cpu_ram(r.cpu)+pen+20,bits,8));
    CHECK(call(&r,0xa87c)==1 && !memcmp(mac_cpu_ram(r.cpu)+back+20,bits,8));
    /* Genuine indexed rendering consumes the changed pen and background. */
    wr(&r,0xb000,4,0);wr(&r,0xb004,2,2);wr(&r,0xb006,2,8);wr(&r,SP,4,0xb000);
    mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_color_raster_service(&r,0xa8a2)==1);
    uint8_t *screen=mac_cpu_ram(r.cpu)+r.screen_base;
    CHECK(screen[0]==255 && screen[1]==0 && screen[640]==0 && screen[641]==255);
    memset(screen,17,8);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_color_raster_service(&r,0xa8a3)==1);
    CHECK(screen[0]==255 && screen[1]==0);
    /* Overlapping source is snapshotted before mutation. */
    memcpy(bits,mac_cpu_ram(r.cpu)+pen+19,8);wr(&r,SP,4,pen+19);
    CHECK(call(&r,0xa89d)==1 && !memcmp(mac_cpu_ram(r.cpu)+pen+20,bits,8));
    mac_cpu_destroy(r.cpu);
}
static void state(void){
    MacRuntime r=fresh();wr(&r,SP,2,3);wr(&r,SP+2,2,5);CHECK(call(&r,0xa89b)==1);
    CHECK(rd(&r,PORT+52,4)==0x00030005);
    wr(&r,SP,2,0xffff);wr(&r,SP+2,2,7);CHECK(call(&r,0xa893)==1);
    wr(&r,SP,4,0xb000);CHECK(call(&r,0xa89a)==1 && rd(&r,0xb000,4)==0xffff0007);
    CHECK(call(&r,0xa896)==1 && call(&r,0xa896)==1 && rd(&r,PORT+66,2)==0xfffe);
    CHECK(call(&r,0xa897)==1 && rd(&r,PORT+66,2)==0xffff);
    wr(&r,SP,2,15);CHECK(call(&r,0xa89c)==1 && rd(&r,PORT+56,2)==15);
    CHECK(call(&r,0xa89e)==1 && rd(&r,PORT+52,4)==0x00010001 && rd(&r,PORT+56,2)==8);
    CHECK(rd(&r,PORT+48,4)==0xffff0007 && rd(&r,PORT+66,2)==0xffff);
    uint32_t pen=rd(&r,rd(&r,PORT+58,4),4);CHECK(rd(&r,pen+20,4)==0xffffffff);
    CHECK(call(&r,0xabcd)==0);wr(&r,PORT+6,2,80);CHECK(call(&r,0xa89d)==0);
    mac_cpu_destroy(r.cpu);
}
static void failures(void){
    for(unsigned kind=0;kind<8;kind++){
        MacRuntime r=fresh();uint32_t h=rd(&r,PORT+58,4),p=rd(&r,h,4);uint16_t op=0xa89d;
        wr(&r,SP,4,0xa000);memset(mac_cpu_ram(r.cpu)+0xa000,0x55,8);
        if(kind==0)wr(&r,h,4,p+2);
        if(kind==1)wr(&r,p,2,1);
        if(kind==2) { for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==h)r.handles[i].resource=1; }
        if(kind==3)wr(&r,SP,4,MAC_RUNTIME_RAM-7);
        if(kind==4){op=0xa89c;wr(&r,SP,2,16);}
        if(kind==5){op=0xa89b;wr(&r,SP,2,0xffff);}
        if(kind==6){op=0xa89e;wr(&r,p,2,1);wr(&r,PORT+52,4,0x00030004);}
        if(kind==7){op=0xa897;wr(&r,PORT+66,2,32767);}
        uint8_t before[108],pattern[28];memcpy(before,mac_cpu_ram(r.cpu)+PORT,108);memcpy(pattern,mac_cpu_ram(r.cpu)+p,28);
        CHECK(call(&r,op)==-1 && mac_cpu_halted(r.cpu) && mac_cpu_get(r.cpu,MAC_A7)==SP);
        CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+PORT,108) && !memcmp(pattern,mac_cpu_ram(r.cpu)+p,28));
        mac_cpu_destroy(r.cpu);
    }
    MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,MAC_RUNTIME_RAM-2);
    CHECK(mac_color_pen_service(&r,0xa89d)==-1);mac_cpu_destroy(r.cpu);
}
int main(void){patterns();state();failures();puts("mac color pen tests passed");return 0;}
