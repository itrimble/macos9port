/* Guest control definitions: the message sequence, argument layout, results, port handling, and corrupted input. The guest's returns are simulated. */
#include "mac_cdef.h"
#include "mac_window_int.h"
#include "mac_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define STORAGE 0x700000u
#define RECT 0x4300u
#define TITLE 0x4400u
#define OUT 0x4600u
static uint8_t fork_image[1024];
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static uint32_t raw(MacRuntime *rt,uint32_t p,unsigned n){const uint8_t *r=mac_cpu_ram(rt->cpu);uint32_t v=0;for(unsigned i=0;i<n;i++)v=v<<8|r[p+i];return v;}
static void put16(uint8_t *p,unsigned x){p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void put32(uint8_t *p,uint32_t x){put16(p,x>>16);put16(p+2,x&0xffff);}
/* A fork with one 'CDEF' id 128 of `size` bytes. */
static size_t build_fork(unsigned size){
    enum{DATA=256};memset(fork_image,0,sizeof fork_image);const unsigned map=DATA+4+size;
    put32(fork_image+DATA,size);for(unsigned i=0;i<size;i++)fork_image[DATA+4+i]=(uint8_t)(0x4e + (i&1));
    put32(fork_image,DATA);put32(fork_image+4,map);put32(fork_image+8,4+size);put32(fork_image+12,50);
    put16(fork_image+map+24,28);put16(fork_image+map+26,50);put16(fork_image+map+28,0);put32(fork_image+map+30,0x43444546);put16(fork_image+map+34,0);put16(fork_image+map+36,10);
    put16(fork_image+map+38,128);put16(fork_image+map+40,0xffff);
    return map+50;
}
static MacRuntime fresh(unsigned cdef_size){
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x100000;rt.heap_limit=0x600000;rt.qd_globals=0x7000;rt.modifiers=0x80;
    CHECK(mac_color_init(&rt)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_toolbox_service(&rt,0xa912)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x12345678);wr(&rt,SP+4,2,12);
    wr(&rt,SP+6,4,0xffffffff);wr(&rt,SP+10,2,12);wr(&rt,SP+12,2,0x0100);
    wr(&rt,SP+14,4,0x4001);wr(&rt,SP+18,4,0x4100);wr(&rt,SP+22,4,STORAGE);
    memcpy(mac_cpu_ram(rt.cpu)+0x4001,"\004Test",5);
    wr(&rt,0x4100,2,40);wr(&rt,0x4102,2,50);wr(&rt,0x4104,2,300);wr(&rt,0x4106,2,450);
    CHECK(mac_window_service(&rt,0xaa45)==1);
    char err[200];if(cdef_size){size_t n=build_fork(cdef_size);CHECK(!mac_resource_open(&rt.fork,fork_image,n,err,sizeof err));}
    return rt;
}
static void pstr(MacRuntime *rt,uint32_t at,const char *s){uint8_t *r=mac_cpu_ram(rt->cpu);r[at]=(uint8_t)strlen(s);memcpy(r+at+1,s,strlen(s));}
static void rect(MacRuntime *rt,int t,int l,int b,int r){wr(rt,RECT,2,(uint16_t)t);wr(rt,RECT+2,2,(uint16_t)l);wr(rt,RECT+4,2,(uint16_t)b);wr(rt,RECT+6,2,(uint16_t)r);}
static int newc(MacRuntime *rt,int vis,unsigned proc,int val,int lo,int hi){
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,0x77);wr(rt,SP+4,2,proc);wr(rt,SP+6,2,(uint16_t)hi);wr(rt,SP+8,2,(uint16_t)lo);wr(rt,SP+10,2,(uint16_t)val);
    wr(rt,SP+12,2,vis?0x100:0);wr(rt,SP+14,4,TITLE);wr(rt,SP+18,4,RECT);wr(rt,SP+22,4,STORAGE);wr(rt,SP+26,4,0xdead);
    return mac_controls_service(rt,0xa954);
}
/* The frame of the guest call in flight: arguments from the lowest address up (param, message, control, varCode). */
static uint32_t a7(MacRuntime *rt){return mac_cpu_get(rt->cpu,MAC_A7);}
static int msg(MacRuntime *rt){return (int16_t)raw(rt,a7(rt)+8,2);}
static uint32_t param(MacRuntime *rt){return raw(rt,a7(rt)+4,4);}
static uint32_t ctl(MacRuntime *rt){return raw(rt,a7(rt)+10,4);}
static int var(MacRuntime *rt){return (int16_t)raw(rt,a7(rt)+14,2);}
static int ret(MacRuntime *rt,uint32_t result){uint32_t a=rt->guest.sp-4;mac_cpu_set(rt->cpu,MAC_A7,a);wr(rt,a,4,result);return mac_guest_return(rt);}
static int running(MacRuntime *rt){return rt->guest.active && !mac_cpu_halted(rt->cpu);}
static uint32_t mk(MacRuntime *rt,int vis,unsigned proc){
    rect(rt,60,60,100,160);pstr(rt,TITLE,"Pop");CHECK(newc(rt,vis,proc,1,0,9)==1);
    if(vis){CHECK(running(rt)&&msg(rt)==CDEF_INIT);CHECK(ret(rt,0)==0&&running(rt)&&msg(rt)==CDEF_DRAW);CHECK(ret(rt,0)==0&&!rt->guest.active);}
    else{CHECK(running(rt)&&msg(rt)==CDEF_INIT);CHECK(ret(rt,0)==0&&!rt->guest.active);}
    CHECK(a7(rt)==SP+26);return raw(rt,SP+26,4);
}
static uint32_t cdef_entry(MacRuntime *rt,uint32_t p){uint32_t dh=rd(rt,p+CTL_DEFPROC,4)&0xffffff;return rd(rt,dh,4);}
static int h_only(MacRuntime *rt,uint16_t op,uint32_t h,unsigned args){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP+args-4,4,h);return mac_controls_service(rt,op);}

int main(void) {
    MacRuntime rt=fresh(64);
    /* NewControl with procID 2051 (CDEF 128, variant 3): initCntl, then drawCntl because it is visible; the result is written once the last call returns. */
    rt.current_port=0x1234;                                                                                  /* not the window: it must be set during the calls and restored */
    rect(&rt,60,60,100,160);pstr(&rt,TITLE,"Pop");
    CHECK(newc(&rt,1,2051,1,0,9)==1 && running(&rt));
    CHECK(msg(&rt)==CDEF_INIT && param(&rt)==0 && var(&rt)==3 && rt.current_port==STORAGE && rd(&rt,rt.qd_globals,4)==STORAGE);
    uint32_t h=ctl(&rt),p=raw(&rt,h,4);
    CHECK(h && !(h&1) && raw(&rt,SP+26,4)==0xdead);                                                          /* not yet delivered */
    CHECK((raw(&rt,p+CTL_DEFPROC,4)>>24)==3 && raw(&rt,p+CTL_DATA,4)==0 && raw(&rt,STORAGE+140,4)==h);       /* variant in the high byte, contrlData left to the CDEF */
    uint32_t code=cdef_entry(&rt,p);CHECK(mac_cpu_get(rt.cpu,MAC_PC)==code && code>=0x400 && !(code&1));
    {const MacHandle *m=NULL;for(size_t i=0;i<rt.handle_count;i++)if(rt.handles[i].handle==(raw(&rt,p+CTL_DEFPROC,4)&0xffffff))m=&rt.handles[i];CHECK(m && m->resource && m->loaded_code && m->type==0x43444546);}
    CHECK(ret(&rt,0)==0 && running(&rt) && msg(&rt)==CDEF_DRAW && param(&rt)==0 && ctl(&rt)==h && var(&rt)==3);
    CHECK(ret(&rt,0)==0 && !rt.guest.active && a7(&rt)==SP+26 && raw(&rt,SP+26,4)==h && rt.current_port==0x1234 && rd(&rt,rt.qd_globals,4)==0x1234);
    /* A hidden control is initialised but not drawn. */
    uint32_t hid=mk(&rt,0,2048);CHECK(hid && hid!=h);
    /* DrawControls: the custom ones are sent drawCntl (hidden ones are skipped, standard ones drawn natively). */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);CHECK(mac_controls_service(&rt,0xa969)==1 && running(&rt) && msg(&rt)==CDEF_DRAW && ctl(&rt)==h);
    CHECK(ret(&rt,0)==0 && !rt.guest.active && a7(&rt)==SP+4);       /* the hidden control (newer, so first in the list) was skipped */
    /* SetCtlValue redraws a custom control; GetCtlValue and friends do not call the CDEF. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,2,5);wr(&rt,SP+2,4,h);CHECK(mac_controls_service(&rt,0xa963)==1 && running(&rt) && msg(&rt)==CDEF_DRAW && ctl(&rt)==h && (int16_t)raw(&rt,p+CTL_VALUE,2)==5);
    CHECK(ret(&rt,0)==0 && a7(&rt)==SP+6);
    wr(&rt,SP+4,2,0x5555);CHECK(h_only(&rt,0xa960,h,4)==1 && !rt.guest.active && raw(&rt,SP+4,2)==5);
    /* Hit testing: testCntl with the point (local coordinates); the part is the answer's low word, clamped. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP+8,4,((uint32_t)70<<16)|80);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,OUT);wr(&rt,SP+12,2,0x5555);wr(&rt,OUT,4,0x99);
    CHECK(mac_controls_service(&rt,0xa96c)==1 && running(&rt) && msg(&rt)==CDEF_TEST && param(&rt)==(((uint32_t)70<<16)|80) && ctl(&rt)==h);
    CHECK(ret(&rt,7)==0 && !rt.guest.active && raw(&rt,OUT,4)==h && (int16_t)raw(&rt,SP+12,2)==7 && a7(&rt)==SP+12);
    static const uint32_t answers[]={0,255,300,0x10000,(uint32_t)-5,254};static const int want[]={0,0,0,0,0,254};
    for(unsigned k=0;k<6;k++){
        mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP+8,4,((uint32_t)70<<16)|80);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,OUT);wr(&rt,SP+12,2,0x5555);wr(&rt,OUT,4,0x99);
        CHECK(mac_controls_service(&rt,0xa96c)==1 && running(&rt));CHECK(ret(&rt,answers[k])==0 && !rt.guest.active);
        CHECK((int16_t)raw(&rt,SP+12,2)==want[k] && raw(&rt,OUT,4)==(want[k]?h:0));
    }
    /* A point outside the control, an inactive or a hidden control never reaches the CDEF. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP+8,4,((uint32_t)5<<16)|5);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,OUT);wr(&rt,OUT,4,0x99);
    CHECK(mac_controls_service(&rt,0xa96c)==1 && !rt.guest.active && raw(&rt,OUT,4)==0 && raw(&rt,SP+12,2)==0);
    wr(&rt,raw(&rt,h,4)+CTL_HILITE,1,255);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP+8,4,((uint32_t)70<<16)|80);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,OUT);
    CHECK(mac_controls_service(&rt,0xa96c)==1 && !rt.guest.active && raw(&rt,OUT,4)==0);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,((uint32_t)70<<16)|80);wr(&rt,SP+4,4,h);wr(&rt,SP+8,2,0x5555);                                    /* TestControl, inactive */
    CHECK(mac_controls_service(&rt,0xa966)==1 && !rt.guest.active && raw(&rt,SP+8,2)==0);
    wr(&rt,raw(&rt,h,4)+CTL_HILITE,1,0);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,((uint32_t)70<<16)|80);wr(&rt,SP+4,4,h);wr(&rt,SP+8,2,0x5555);                                    /* TestControl, active */
    CHECK(mac_controls_service(&rt,0xa966)==1 && running(&rt) && msg(&rt)==CDEF_TEST);CHECK(ret(&rt,9)==0 && (int16_t)raw(&rt,SP+8,2)==9 && a7(&rt)==SP+8);
    /* A Control Manager call made by the CDEF itself (while a call is in flight) halts. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);CHECK(mac_controls_service(&rt,0xa969)==1 && running(&rt));
    {uint32_t a=a7(&rt);mac_cpu_set(rt.cpu,MAC_A7,SP-0x100);wr(&rt,SP-0x100,4,h);CHECK(mac_controls_service(&rt,0xa960)==-1 && mac_cpu_halted(rt.cpu));(void)a;}   /* even a call that sends no message */
    mac_cpu_destroy(rt.cpu);
    /* DisposeControl sends dispCntl first, then the control is gone; KillControls does the same for each. */
    rt=fresh(64);h=mk(&rt,1,2048);uint32_t h2=mk(&rt,0,2048);
    CHECK(h_only(&rt,0xa955,h,4)==1 && running(&rt) && msg(&rt)==CDEF_DISP && ctl(&rt)==h && raw(&rt,h,4)!=0);
    CHECK(ret(&rt,0)==0 && !rt.guest.active && raw(&rt,h,4)==0 && raw(&rt,STORAGE+140,4)==h2 && a7(&rt)==SP+4);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);CHECK(mac_controls_service(&rt,0xa956)==1 && running(&rt) && msg(&rt)==CDEF_DISP && ctl(&rt)==h2);
    CHECK(ret(&rt,0)==0 && raw(&rt,h2,4)==0 && raw(&rt,STORAGE+140,4)==0 && a7(&rt)==SP+4);
    mac_cpu_destroy(rt.cpu);
    /* TrackControl(ctl, startPt, -1): testCntl, then (if it found a part) autoTrack with that part as the parameter; the result is the part. */
    {   MacRuntime tr = fresh(64); uint32_t th = mk(&tr, 1, 2048); const uint32_t pt = ((uint32_t)70 << 16) | 80;
        mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0xffffffffu); wr(&tr, SP + 4, 4, pt); wr(&tr, SP + 8, 4, th); wr(&tr, SP + 12, 2, 0x5555);
        CHECK(mac_controls_service(&tr, 0xa968) == 1 && running(&tr) && msg(&tr) == CDEF_TEST && param(&tr) == pt && ctl(&tr) == th);
        CHECK(ret(&tr, 10) == 0 && running(&tr) && msg(&tr) == CDEF_AUTOTRACK && param(&tr) == 10 && ctl(&tr) == th);
        CHECK(ret(&tr, 99) == 0 && !tr.guest.active && a7(&tr) == SP + 12 && (int16_t)raw(&tr, SP + 12, 2) == 10);               /* the answer to autoTrack does not change the result */
        /* testCntl finds nothing: no autoTrack, result 0 */
        mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0xffffffffu); wr(&tr, SP + 4, 4, pt); wr(&tr, SP + 8, 4, th); wr(&tr, SP + 12, 2, 0x5555);
        CHECK(mac_controls_service(&tr, 0xa968) == 1 && running(&tr) && msg(&tr) == CDEF_TEST);
        CHECK(ret(&tr, 0) == 0 && !tr.guest.active && a7(&tr) == SP + 12 && raw(&tr, SP + 12, 2) == 0);
        /* an inactive control is not tracked */
        wr(&tr, raw(&tr, th, 4) + CTL_HILITE, 1, 255);
        mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0xffffffffu); wr(&tr, SP + 4, 4, pt); wr(&tr, SP + 8, 4, th); wr(&tr, SP + 12, 2, 0x5555);
        CHECK(mac_controls_service(&tr, 0xa968) == 1 && !tr.guest.active && raw(&tr, SP + 12, 2) == 0);
        mac_cpu_destroy(tr.cpu); }
    {   MacRuntime tr = fresh(64); uint32_t th = mk(&tr, 1, 2048);                                                          /* an action procedure (or none) is not supported */
        mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0x1000); wr(&tr, SP + 4, 4, 0); wr(&tr, SP + 8, 4, th);
        CHECK(mac_controls_service(&tr, 0xa968) == -1 && mac_cpu_halted(tr.cpu)); mac_cpu_destroy(tr.cpu); }
    {   MacRuntime tr = fresh(64); uint32_t th = mk(&tr, 1, 2048);
        mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0); wr(&tr, SP + 4, 4, 0); wr(&tr, SP + 8, 4, th);
        CHECK(mac_controls_service(&tr, 0xa968) == -1 && mac_cpu_halted(tr.cpu)); mac_cpu_destroy(tr.cpu); }
    {   MacRuntime tr = fresh(64); rect(&tr, 60, 60, 80, 160); pstr(&tr, TITLE, "x"); CHECK(newc(&tr, 1, 0, 1, 0, 9) == 1);   /* a standard control */
        uint32_t sh = raw(&tr, SP + 26, 4); mac_cpu_set(tr.cpu, MAC_A7, SP); wr(&tr, SP, 4, 0xffffffffu); wr(&tr, SP + 4, 4, 0); wr(&tr, SP + 8, 4, sh);
        CHECK(mac_controls_service(&tr, 0xa968) == -1 && mac_cpu_halted(tr.cpu)); mac_cpu_destroy(tr.cpu); }
    /* Corrupted guest input. */
    for(unsigned k=0;k<9;k++){
        MacRuntime r=fresh(k==7?8:64);int rc=0;
        if(k==7){rect(&r,60,60,100,160);pstr(&r,TITLE,"Pop");rc=newc(&r,1,2048,1,0,9);CHECK(rc==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);continue;}   /* a CDEF resource too small to be code */
        uint32_t c=mk(&r,1,2048),pp=raw(&r,c,4);uint32_t dh=raw(&r,pp+CTL_DEFPROC,4)&0xffffff;
        switch(k){
        case 0:wr(&r,pp+CTL_DEFPROC,4,0x4800);break;                                                         /* not a handle in the table */
        case 1:wr(&r,pp+CTL_DEFPROC,4,c);break;                                                              /* a control handle, not a CDEF */
        case 2:wr(&r,dh,4,raw(&r,dh,4)+16);break;                                                            /* the CDEF's master pointer moved */
        case 3:wr(&r,pp+CTL_DEFPROC,4,dh|0x01000001);break;                                                  /* odd handle */
        case 4:{for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==dh)r.handles[i].loaded_code=0;}break;   /* no longer pinned */
        case 5:{for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==dh)r.handles[i].type=0x1234;}break;    /* not a CDEF any more */
        case 8:{for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==dh)r.handles[i].resource=0;}break;       /* not a resource handle */
        default:wr(&r,pp+CTL_OWNER,4,0x1234);break;                                                          /* owner window forged */
        }
        mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,STORAGE);rc=mac_controls_service(&r,0xa969);
        CHECK(rc==-1 && mac_cpu_halted(r.cpu) && !r.guest.active);mac_cpu_destroy(r.cpu);
    }
    /* No CDEF resource at all: halts by name. Standard procIDs never call guest code. */
    {MacRuntime r=fresh(0);rect(&r,60,60,100,160);pstr(&r,TITLE,"x");CHECK(newc(&r,1,2048,1,0,9)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    {MacRuntime r=fresh(64);rect(&r,60,60,100,160);pstr(&r,TITLE,"x");CHECK(newc(&r,1,0,1,0,9)==1 && !r.guest.active && a7(&r)==SP+26 && raw(&r,SP+26,4)!=0xdead);mac_cpu_destroy(r.cpu);}
    puts("Guest control definition checks passed");return 0;
}
