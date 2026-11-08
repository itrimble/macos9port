#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_window_resource_service(MacRuntime*,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
static void w16(uint8_t*p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void w32(uint8_t*p,uint32_t v){w16(p,v>>16);w16(p+2,v);}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
/* Entirely authored synthetic resource fork. No application fixture bytes. */
static void fixture(uint8_t*b,unsigned size) {
    memset(b,0,2048);w32(b,256);w32(b+4,1024);w32(b+8,size+4);w32(b+12,50);
    w32(b+256,size);uint8_t*d=b+260;
    w16(d,80);w16(d+2,90);w16(d+4,280);w16(d+6,390);w16(d+8,12);
    d[10]=1;d[12]=1;w32(d+14,0x12345678);d[18]=4;memcpy(d+19,"Test",4);
    w16(b+1048,28);w16(b+1050,50);w16(b+1052,0);
    memcpy(b+1054,"WIND",4);w16(b+1058,0);w16(b+1060,10);
    w16(b+1062,129);w16(b+1064,0xffff);
}
static MacRuntime fresh(uint8_t*b) {
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    r.heap_next=0x100000;r.heap_limit=0x600000;r.qd_globals=0x7000;r.modifiers=0x80;
    CHECK(!mac_resource_open(&r.fork,b,2048,NULL,0));CHECK(mac_color_init(&r)==1);
    mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_toolbox_service(&r,0xa912)==1);
    memset(mac_cpu_ram(r.cpu)+SP-294,0x5a,308);
    w32(mac_cpu_ram(r.cpu)+SP,UINT32_MAX);w32(mac_cpu_ram(r.cpu)+SP+4,0);
    w16(mac_cpu_ram(r.cpu)+SP+8,129);mac_cpu_set(r.cpu,MAC_A7,SP);return r;
}
static void successful(int edited) {
    uint8_t b[2048];fixture(b,23);MacRuntime r=fresh(b);
    if(edited) {
        uint32_t h=mac_runtime_resource(&r,0x57494e44,129);CHECK(h);
        uint32_t p=rd(&r,h,4);mac_cpu_ram(r.cpu)[p+10]=0;mac_cpu_ram(r.cpu)[p+19]='B';
    }
    uint8_t args[10];memcpy(args,mac_cpu_ram(r.cpu)+SP,10);
    CHECK(mac_window_resource_service(&r,0xaa46)==1 && r.window.address);
    CHECK(rd(&r,SP+10,4)==r.window.address && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
    CHECK(!memcmp(args,mac_cpu_ram(r.cpu)+SP,10));
    for(unsigned i=0;i<294;i++)CHECK(mac_cpu_ram(r.cpu)[SP-294+i]==0x5a);
    CHECK(r.window.bounds[0]==80 && r.window.bounds[1]==90 && r.window.bounds[2]==280);
    CHECK(r.window.proc_id==12 && r.window.visible==!edited);
    CHECK(r.window.title[0]==4 && r.window.title[1]==(edited?'B':'T'));
    CHECK(rd(&r,r.window.address+152,4)==0x12345678);
    CHECK(rd(&r,0xa60,2)==0);mac_cpu_destroy(r.cpu);
}
static void plain_box(void) {
    uint8_t b[2048];fixture(b,23);w16(b+268,2);b[270]=0;b[272]=0;
    MacRuntime r=fresh(b);CHECK(mac_window_resource_service(&r,0xaa46)==1);
    CHECK(r.window.address && r.window.proc_id==2 && !r.window.visible);
    uint32_t structure=rd(&r,rd(&r,r.window.address+114,4),4);
    CHECK(rd(&r,structure+2,2)==79 && rd(&r,structure+4,2)==89);
    CHECK(rd(&r,structure+6,2)==281 && rd(&r,structure+8,2)==391);
    mac_cpu_destroy(r.cpu);
}
static void stray_bytes(void) {   /* bytes after the title beyond the positioning word are ignored (Room Editor's WIND 128 carries 250) */
    uint8_t b[2048];fixture(b,60);memset(b+260+23,0x6f,37);
    MacRuntime r=fresh(b);CHECK(mac_window_resource_service(&r,0xaa46)==1);
    CHECK(r.window.address && !mac_cpu_halted(r.cpu) && r.window.title[0]==4 && r.window.title[1]=='T');
    mac_cpu_destroy(r.cpu);
}
static void failures(void) {
    for(unsigned kind=0;kind<7;kind++) {
        uint8_t b[2048];fixture(b,kind==1?20:kind==2?26:23);
        if(kind==2)w16(b+260+24,0x280a);
        if(kind==3)w16(b+260+8,99);
        MacRuntime r=fresh(b);uint32_t before=r.heap_next;
        if(kind==0)w16(mac_cpu_ram(r.cpu)+SP+8,130);
        if(kind==4)w32(mac_cpu_ram(r.cpu)+SP+4,SP-100);
        if(kind==5)r.heap_limit=r.heap_next+100;
        if(kind==6)mac_cpu_set(r.cpu,MAC_A7,MAC_RUNTIME_RAM-10);
        int result=mac_window_resource_service(&r,0xaa46);
        if(kind==0 || kind==5) {
            CHECK(result==1 && !r.window.address && !rd(&r,SP+10,4));
            CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+10);
            CHECK((int16_t)rd(&r,kind==0?0xa60:0x220,2)==(kind==0?-192:-108));
        } else CHECK(result==-1 && mac_cpu_halted(r.cpu) && !r.window.address);
        CHECK(r.heap_next==before);
        for(unsigned i=0;i<294;i++)CHECK(mac_cpu_ram(r.cpu)[SP-294+i]==0x5a);
        mac_cpu_destroy(r.cpu);
    }
}
int main(void){successful(0);successful(1);plain_box();stray_bytes();failures();puts("Resource-backed color windows: ok");return 0;}
