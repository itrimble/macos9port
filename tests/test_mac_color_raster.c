#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
int mac_color_raster_service(MacRuntime *,uint16_t);
static int failures;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
enum { PORT=0x100, PMH=0x200, PM=0x210, RH=0x250, R=0x260, PH=0x280, P=0x290, RECT=0x300, SP=0x400, PIX=0x800 };
static void w(MacRuntime *r,uint32_t a,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,a,n,v));}
static void rect(MacRuntime *r,uint32_t a,int t,int l,int b,int rr){w(r,a,2,t);w(r,a+2,2,l);w(r,a+4,2,b);w(r,a+6,2,rr);}
static MacRuntime setup(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(8192);r.qd_globals=0x80;
    w(&r,0x80,4,PORT);w(&r,PORT+6,2,0xc000);w(&r,PORT+2,4,PMH);w(&r,PMH,4,PM);
    w(&r,PM,4,PIX);w(&r,PM+4,2,0x8008);rect(&r,PM+6,0,0,4,8);
    w(&r,PM+32,2,8);w(&r,PM+34,2,1);w(&r,PM+36,2,8);rect(&r,PORT+16,0,0,4,8);
    w(&r,PORT+24,4,RH);w(&r,PORT+28,4,RH);w(&r,RH,4,R);w(&r,R,2,10);rect(&r,R+2,0,0,4,8);
    w(&r,PORT+32,4,PH);w(&r,PORT+58,4,PH);w(&r,PH,4,P);memset(mac_cpu_ram(r.cpu)+P+20,0xaa,8);
    w(&r,PORT+80,4,3);w(&r,PORT+84,4,7);w(&r,PORT+52,2,1);w(&r,PORT+54,2,1);w(&r,PORT+56,2,8);
    rect(&r,RECT,0,0,4,8);return r;
}
static int call(MacRuntime *r,uint16_t op){mac_cpu_set(r->cpu,MAC_A7,SP);w(r,SP,4,RECT);return mac_color_raster_service(r,op);}
int main(void){
    MacRuntime r=setup();uint8_t *mem=mac_cpu_ram(r.cpu);
    const uint8_t expected[8][2]={{3,7},{3,9},{246,9},{7,9},{7,3},{9,3},{9,246},{9,7}};
    for(int mode=8;mode<16;mode++){
        memset(mem+PIX,9,32);w(&r,PORT+56,2,mode);CHECK(call(&r,0xa8a2)==1);
        CHECK(mem[PIX]==expected[mode-8][0] && mem[PIX+1]==expected[mode-8][1]);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    }
    w(&r,PORT+56,2,8);memset(mem+PIX,9,32);rect(&r,R+2,1,2,3,6);
    CHECK(call(&r,0xa8a2)==1);CHECK(mem[PIX]==9 && mem[PIX+8+2]==3 && mem[PIX+8+1]==9 && mem[PIX+8+6]==9);
    rect(&r,R+2,0,0,4,8);memset(mem+PIX,9,32);CHECK(call(&r,0xa8a1)==1);
    CHECK(mem[PIX+8+2]==9 && mem[PIX+2]==3 && mem[PIX+3*8+2]==3);
    CHECK(call(&r,0xa8a3)==1);CHECK(mem[PIX]==3 && mem[PIX+1]==7);
    CHECK(call(&r,0xa8a4)==1);CHECK(mem[PIX]==252 && mem[PIX+1]==248);
    mac_cpu_set(r.cpu,MAC_A7,SP);w(&r,SP,4,P+20);w(&r,SP+4,4,RECT);
    CHECK(mac_color_raster_service(&r,0xa8a5)==1);CHECK(mem[PIX]==3 && mem[PIX+1]==7);
    CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+8);
    r.palette[19]=0xff0000;w(&r,RECT,2,65535);w(&r,RECT+2,2,0);w(&r,RECT+4,2,0);
    CHECK(call(&r,0xaa14)==1);CHECK(mac_cpu_read(r.cpu,PORT+80,4)==19);CHECK(mac_cpu_read(r.cpu,PORT+36,2)==65535);
    CHECK(call(&r,0xaa15)==1);CHECK(mac_cpu_read(r.cpu,PORT+84,4)==19);
    w(&r,PORT+6,2,8);CHECK(call(&r,0xa8a2)==0);CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);
    mac_cpu_destroy(r.cpu);
    for(int error=0;error<4;error++){
        r=setup();mem=mac_cpu_ram(r.cpu);memset(mem+PIX,99,32);
        if(error==0)w(&r,P,2,1);
        if(error==1)w(&r,R,2,12);
        if(error==2)w(&r,PORT+56,2,32);
        if(error==3)w(&r,PM,4,8190);
        CHECK(call(&r,0xa8a2)==-1);CHECK(mac_cpu_halted(r.cpu));
        for(int i=0;i<32;i++)CHECK(mem[PIX+i]==99);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);mac_cpu_destroy(r.cpu);
    }
    printf("Color raster checks: %s\n",failures?"FAILED":"ok");return failures!=0;
}
