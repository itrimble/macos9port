/* Control Manager subset: records, values, hit testing, drawing, GetNewControl, and corrupted guest input. */
#include "mac_controls.h"
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
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static uint32_t raw(MacRuntime *rt,uint32_t p,unsigned n){const uint8_t *r=mac_cpu_ram(rt->cpu);uint32_t v=0;for(unsigned i=0;i<n;i++)v=v<<8|r[p+i];return v;}
static uint8_t fork_image[512];
static void put16(uint8_t *p,unsigned x){p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void put32(uint8_t *p,uint32_t x){put16(p,x>>16);put16(p+2,x&0xffff);}
/* A resource fork with one CNTL (id 128): rect, value, visible, max, min, procID, refCon, title. */
static size_t build_fork(unsigned proc,int vis,int val,int lo,int hi,unsigned shorten){
    enum{DATA=256,L=26};memset(fork_image,0,sizeof fork_image);uint8_t *d=fork_image+DATA+4;const unsigned map=DATA+4+L;
    put32(fork_image+DATA,L);put16(d,70);put16(d+2,60);put16(d+4,90);put16(d+6,120);put16(d+8,val);d[10]=(uint8_t)vis;put16(d+12,hi);put16(d+14,lo);put16(d+16,proc);put32(d+18,0x99);d[22]=3;memcpy(d+23,"Go!",3);
    put32(fork_image,DATA);put32(fork_image+4,map);put32(fork_image+8,4+L);put32(fork_image+12,50);
    put16(fork_image+map+24,28);put16(fork_image+map+26,50);put16(fork_image+map+28,0);put32(fork_image+map+30,0x434e544c);put16(fork_image+map+34,0);put16(fork_image+map+36,10);
    put16(fork_image+map+38,128);put16(fork_image+map+40,0xffff);
    if(shorten)put32(fork_image+DATA,L-shorten);
    return map+50;
}
static MacRuntime fresh_proc(unsigned proc) {
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x100000;rt.heap_limit=0x600000;rt.qd_globals=0x7000;rt.modifiers=0x80;
    CHECK(mac_color_init(&rt)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_toolbox_service(&rt,0xa912)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x12345678);wr(&rt,SP+4,2,0x01aa);
    wr(&rt,SP+6,4,0xffffffff);wr(&rt,SP+10,2,proc);wr(&rt,SP+12,2,0x0100);
    wr(&rt,SP+14,4,0x4001);wr(&rt,SP+18,4,0x4100);wr(&rt,SP+22,4,STORAGE);
    memcpy(mac_cpu_ram(rt.cpu)+0x4001,"\004Test",5);
    wr(&rt,0x4100,2,40);wr(&rt,0x4102,2,50);wr(&rt,0x4104,2,300);wr(&rt,0x4106,2,450);
    CHECK(mac_window_service(&rt,0xaa45)==1);
    return rt;
}
static MacRuntime fresh(void){return fresh_proc(12);}
static void pstr(MacRuntime *rt,uint32_t at,const char *s){uint8_t *r=mac_cpu_ram(rt->cpu);r[at]=(uint8_t)strlen(s);memcpy(r+at+1,s,strlen(s));}
static void rect(MacRuntime *rt,int t,int l,int b,int r){wr(rt,RECT,2,(uint16_t)t);wr(rt,RECT+2,2,(uint16_t)l);wr(rt,RECT+4,2,(uint16_t)b);wr(rt,RECT+6,2,(uint16_t)r);}
/* NewControl(window, rect, title, visible, value, min, max, procID, refCon) */
static uint32_t newc(MacRuntime *rt,uint32_t win,uint32_t bounds,uint32_t title,int vis,int val,int lo,int hi,unsigned proc,uint32_t refcon,int *rc){
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,refcon);wr(rt,SP+4,2,proc);wr(rt,SP+6,2,(uint16_t)hi);wr(rt,SP+8,2,(uint16_t)lo);wr(rt,SP+10,2,(uint16_t)val);
    wr(rt,SP+12,2,vis?0x100:0);wr(rt,SP+14,4,title);wr(rt,SP+18,4,bounds);wr(rt,SP+22,4,win);wr(rt,SP+26,4,0xdead);
    *rc=mac_controls_service(rt,0xa954);return *rc==1?raw(rt,SP+26,4):0;
}
static uint32_t mk(MacRuntime *rt,int t,int l,int b,int r,int val,int lo,int hi,unsigned proc,const char *title){
    int rc;rect(rt,t,l,b,r);pstr(rt,TITLE,title);uint32_t h=newc(rt,STORAGE,RECT,TITLE,1,val,lo,hi,proc,0x1234,&rc);CHECK(rc==1&&h);return h;}
/* A call with the handle (or window) deepest on the stack and `n` trailing words/longs written by the caller before. */
static int call(MacRuntime *rt,uint16_t op,unsigned args){mac_cpu_set(rt->cpu,MAC_A7,SP);int rc=mac_controls_service(rt,op);if(rc==1)CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP+args);return rc;}
static int h_only(MacRuntime *rt,uint16_t op,uint32_t h,unsigned args){wr(rt,SP+args-4,4,h);return call(rt,op,args);}
static int h_word(MacRuntime *rt,uint16_t op,uint32_t h,int v){wr(rt,SP,2,(uint16_t)v);wr(rt,SP+2,4,h);return call(rt,op,6);}
static int h_two(MacRuntime *rt,uint16_t op,uint32_t h,int a,int b){wr(rt,SP,2,(uint16_t)b);wr(rt,SP+2,2,(uint16_t)a);wr(rt,SP+4,4,h);return call(rt,op,8);}
static int h_long(MacRuntime *rt,uint16_t op,uint32_t h,uint32_t v){wr(rt,SP,4,v);wr(rt,SP+4,4,h);return call(rt,op,8);}
static uint32_t P(MacRuntime *rt,uint32_t h){return rd(rt,h,4);}
static int get16(MacRuntime *rt,uint16_t op,uint32_t h){wr(rt,SP+4,2,0x5555);CHECK(h_only(rt,op,h,4)==1);return (int16_t)rd(rt,SP+4,2);}
static int part_at(MacRuntime *rt,uint32_t h,int v,int hh){wr(rt,SP,4,((uint32_t)(uint16_t)v<<16)|(uint16_t)hh);wr(rt,SP+4,4,h);wr(rt,SP+8,2,0x5555);CHECK(call(rt,0xa966,8)==1);return (int16_t)rd(rt,SP+8,2);}
static int screen_sum(MacRuntime *rt,int l,int t,int r,int b){uint8_t *m=mac_cpu_ram(rt->cpu);unsigned s=0;for(int y=t;y<b;y++)for(int x=l;x<r;x++)s=s*31+m[rt->screen_base+(uint32_t)y*rt->screen_row_bytes+(uint32_t)x]+1;return (int)s;}

int main(void) {
    MacRuntime rt=fresh();
    CHECK(mac_controls_service(&rt,0xa873)==0 && mac_controls_service(&rt,0xa970)==0);                       /* neighbours are not ours */
    /* NewControl: record fields, value clamped, the handle is nonzero and even, ResError 0, window control list linked newest first. */
    uint32_t b1=mk(&rt,60,60,80,160,5,0,1,0,"OK");
    uint32_t p1=P(&rt,b1);CHECK(!(b1&1)&&p1);
    CHECK(raw(&rt,p1+CTL_OWNER,4)==STORAGE && (int16_t)raw(&rt,p1+CTL_RECT,2)==60 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==160 && raw(&rt,p1+CTL_VIS,1)==1);
    CHECK((int16_t)raw(&rt,p1+CTL_VALUE,2)==1 && raw(&rt,p1+CTL_REFCON,4)==0x1234 && raw(&rt,p1+CTL_TITLE,1)==2 && raw(&rt,p1+CTL_TITLE+1,1)=='O');
    CHECK(raw(&rt,0xa60,2)==0 && raw(&rt,STORAGE+140,4)==b1 && raw(&rt,p1+CTL_NEXT,4)==0);
    uint32_t b2=mk(&rt,100,60,112,160,0,0,1,1,"Check");uint32_t p2=P(&rt,b2);
    uint32_t b3=mk(&rt,100,170,112,260,0,0,1,2,"Radio");
    uint32_t b4=mk(&rt,40,430,260,446,10,0,100,16,"");uint32_t p4=P(&rt,b4);                              /* a vertical scroll bar */
    uint32_t b5=mk(&rt,280,50,296,400,0,0,0,24,"");                                                         /* a horizontal scroll bar with useWFont, no range */
    CHECK(raw(&rt,STORAGE+140,4)==b5 && raw(&rt,P(&rt,b5)+CTL_NEXT,4)==b4 && raw(&rt,p4+CTL_NEXT,4)==b3 && raw(&rt,p2+CTL_NEXT,4)==b1);
    /* Values: get/set, clamping, the minimum and maximum, refCon, hilite. */
    CHECK(get16(&rt,0xa960,b4)==10 && get16(&rt,0xa961,b4)==0 && get16(&rt,0xa962,b4)==100);
    CHECK(h_word(&rt,0xa963,b4,500)==1 && get16(&rt,0xa960,b4)==100);
    CHECK(h_word(&rt,0xa963,b4,-5)==1 && get16(&rt,0xa960,b4)==0);
    CHECK(h_word(&rt,0xa963,b4,50)==1 && h_word(&rt,0xa965,b4,20)==1 && get16(&rt,0xa962,b4)==20 && get16(&rt,0xa960,b4)==20);   /* lowering the max clamps the value */
    CHECK(h_word(&rt,0xa964,b4,30)==1 && !mac_cpu_halted(rt.cpu) && get16(&rt,0xa961,b4)==30 && get16(&rt,0xa962,b4)==20 && get16(&rt,0xa960,b4)==20);   /* min above max is stored as given; the value settles on the maximum */
    mac_cpu_destroy(rt.cpu);rt=fresh();
    b1=mk(&rt,60,60,80,160,5,0,1,0,"OK");p1=P(&rt,b1);
    wr(&rt,SP+4,4,0);CHECK(h_only(&rt,0xa95a,b1,4)==1 && raw(&rt,SP+4,4)==0x1234);
    CHECK(h_long(&rt,0xa95b,b1,0xcafe)==1 && raw(&rt,p1+CTL_REFCON,4)==0xcafe);
    CHECK(h_word(&rt,0xa95d,b1,255)==1 && raw(&rt,p1+CTL_HILITE,1)==255 && part_at(&rt,b1,70,70)==0);       /* inactive: no part */
    CHECK(h_word(&rt,0xa95d,b1,0)==1 && part_at(&rt,b1,70,70)==10 && part_at(&rt,b1,59,70)==0 && part_at(&rt,b1,80,70)==0 && part_at(&rt,b1,70,160)==0 && part_at(&rt,b1,79,159)==10);
    /* Titles. */
    pstr(&rt,TITLE,"Hello");CHECK(h_long(&rt,0xa95f,b1,TITLE)==1 && raw(&rt,p1+CTL_TITLE,1)==5);
    memset(mac_cpu_ram(rt.cpu)+0x4500,0xee,16);CHECK(h_long(&rt,0xa95e,b1,0x4500)==1 && raw(&rt,0x4500,1)==5 && raw(&rt,0x4505,1)=='o' && raw(&rt,0x4506,1)==0xee);
    /* Show, hide, move, size, hit testing across the controls. */
    uint32_t sb=mk(&rt,40,430,260,446,50,0,100,16,"");CHECK(raw(&rt,STORAGE+140,4)==sb);
    CHECK(part_at(&rt,sb,45,438)==20 && part_at(&rt,sb,255,438)==21 && part_at(&rt,sb,150,438)==129 && part_at(&rt,sb,100,438)==22 && part_at(&rt,sb,220,438)==23);
    CHECK(part_at(&rt,sb,55,438)==20 && part_at(&rt,sb,56,438)==22 && part_at(&rt,sb,243,438)==23 && part_at(&rt,sb,244,438)==21);   /* arrow cells are 16 pixels */
    CHECK(part_at(&rt,sb,141,438)==22 && part_at(&rt,sb,142,438)==129 && part_at(&rt,sb,157,438)==129 && part_at(&rt,sb,158,438)==23);   /* the thumb is 16 pixels */
    CHECK(h_word(&rt,0xa963,sb,0)==1 && part_at(&rt,sb,60,438)==129 && part_at(&rt,sb,100,438)==23);
    wr(&rt,SP+8,4,((uint32_t)45<<16)|438);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,0x4600);wr(&rt,SP+12,2,0x5555);CHECK(call(&rt,0xa96c,12)==1 && raw(&rt,0x4600,4)==sb && (int16_t)raw(&rt,SP+12,2)==20);
    wr(&rt,SP+8,4,((uint32_t)70<<16)|70);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,0x4600);CHECK(call(&rt,0xa96c,12)==1 && raw(&rt,0x4600,4)==b1 && (int16_t)raw(&rt,SP+12,2)==10);
    wr(&rt,SP+8,4,((uint32_t)5<<16)|5);wr(&rt,SP+4,4,STORAGE);wr(&rt,SP,4,0x4600);CHECK(call(&rt,0xa96c,12)==1 && raw(&rt,0x4600,4)==0 && raw(&rt,SP+12,2)==0);
    CHECK(h_only(&rt,0xa958,b1,4)==1 && raw(&rt,p1+CTL_VIS,1)==0 && part_at(&rt,b1,70,70)==0);
    CHECK(h_only(&rt,0xa957,b1,4)==1 && raw(&rt,p1+CTL_VIS,1)==1);
    CHECK(h_two(&rt,0xa959,b1,200,150)==1 && (int16_t)raw(&rt,p1+CTL_RECT,2)==150 && (int16_t)raw(&rt,p1+CTL_RECT+2,2)==200 && (int16_t)raw(&rt,p1+CTL_RECT+4,2)==170 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==300);
    CHECK(h_two(&rt,0xa95c,b1,50,30)==1 && (int16_t)raw(&rt,p1+CTL_RECT+4,2)==180 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==250);
    /* SizeControl does no validation: a zero or negative size (a pane that is still 0 wide asks for width - 15) leaves an empty rectangle that
     * hit-tests as nothing and can be sized again; the call itself succeeds. */
    CHECK(h_two(&rt,0xa95c,b1,-15,16)==1 && !mac_cpu_halted(rt.cpu) && (int16_t)raw(&rt,p1+CTL_RECT,2)==150 && (int16_t)raw(&rt,p1+CTL_RECT+4,2)==166 && (int16_t)raw(&rt,p1+CTL_RECT+2,2)==200 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==185);
    CHECK(part_at(&rt,b1,155,240)==0 && part_at(&rt,b1,155,190)==0 && part_at(&rt,b1,155,200)==0);
    CHECK(h_two(&rt,0xa95c,b1,0,0)==1 && (int16_t)raw(&rt,p1+CTL_RECT+4,2)==150 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==200 && part_at(&rt,b1,150,200)==0);
    CHECK(h_two(&rt,0xa95c,b1,50,30)==1 && (int16_t)raw(&rt,p1+CTL_RECT+4,2)==180 && (int16_t)raw(&rt,p1+CTL_RECT+6,2)==250);
    /* Drawing changes the screen inside the window and nothing outside it. */
    uint8_t *m=mac_cpu_ram(rt.cpu);int out=screen_sum(&rt,460,40,520,300);
    for(int y=190;y<220;y++)memset(m+rt.screen_base+(size_t)y*rt.screen_row_bytes+250,0x55,50);             /* paint over the button, then redraw */
    int before=screen_sum(&rt,250,190,300,220);
    wr(&rt,SP,4,STORAGE);CHECK(call(&rt,0xa969,4)==1);CHECK(screen_sum(&rt,250,190,300,220)!=before && screen_sum(&rt,460,40,520,300)==out);
    /* Dispose unlinks and invalidates the handle. */
    CHECK(h_only(&rt,0xa955,b1,4)==1 && raw(&rt,b1,4)==0);
    CHECK(raw(&rt,STORAGE+140,4)==sb && raw(&rt,P(&rt,sb)+CTL_NEXT,4)==0);
    CHECK(h_only(&rt,0xa955,b1,4)==-1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);rt=fresh();
    /* KillControls disposes them all. */
    {uint32_t a=mk(&rt,60,60,80,160,0,0,1,0,"A"),b=mk(&rt,100,60,112,160,0,0,1,1,"B");(void)a;wr(&rt,SP,4,STORAGE);CHECK(call(&rt,0xa956,4)==1 && raw(&rt,STORAGE+140,4)==0 && raw(&rt,b,4)==0);}
    /* GetNewControl: a missing CNTL gives resNotFound and a nil handle; (the fixture has no CNTL resource). */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);wr(&rt,SP+4,2,128);wr(&rt,SP+6,4,0xdead);CHECK(mac_controls_service(&rt,0xa9be)==1 && raw(&rt,SP+6,4)==0 && (int16_t)raw(&rt,0xa60,2)==-192 && mac_cpu_get(rt.cpu,MAC_A7)==SP+6);
    /* GetNewControl with a CNTL: the fields come from the resource and the control is linked and drawn like NewControl's. */
    {MacRuntime r=fresh();char err[200];size_t n=build_fork(16,1,3,0,5,0);CHECK(!mac_resource_open(&r.fork,fork_image,n,err,sizeof err));
     mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,STORAGE);wr(&r,SP+4,2,128);wr(&r,SP+6,4,0xdead);
     CHECK(mac_controls_service(&r,0xa9be)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+6);uint32_t h=raw(&r,SP+6,4),p=P(&r,h);
     CHECK(h && (int16_t)raw(&r,p+CTL_RECT,2)==70 && (int16_t)raw(&r,p+CTL_RECT+6,2)==120 && (int16_t)raw(&r,p+CTL_VALUE,2)==3 && (int16_t)raw(&r,p+CTL_MAX,2)==5 && (int16_t)raw(&r,p+CTL_MIN,2)==0);
     CHECK(raw(&r,p+CTL_VIS,1)==1 && raw(&r,p+CTL_DATA,4)==16 && raw(&r,p+CTL_REFCON,4)==0x99 && raw(&r,p+CTL_TITLE,1)==3 && raw(&r,p+CTL_TITLE+3,1)=='!' && raw(&r,STORAGE+140,4)==h && raw(&r,p+CTL_OWNER,4)==STORAGE);
     /* a CNTL whose procID is a custom definition halts by name; a truncated resource halts */
     mac_cpu_destroy(r.cpu);}
    for(unsigned k=0;k<3;k++){MacRuntime r=fresh();char err[200];size_t n=k==0?build_fork(2064,1,0,0,1,0):k==1?build_fork(0,1,0,0,1,22):build_fork(0,1,0,9,1,0);CHECK(!mac_resource_open(&r.fork,fork_image,n,err,sizeof err));
     mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,k==2?STORAGE:STORAGE);wr(&r,SP+4,2,128);CHECK(mac_controls_service(&r,0xa9be)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    /* Unsupported pieces halt by name. */
    {MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_controls_service(&r,0xa968)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    {MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_controls_service(&r,0xa967)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    {static const unsigned bad[]={3,4,5,6,7,11,12,15,17,32,2064,4095};   /* 8-10 and 24 are the useWFont variants of the supported ones */
     for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){MacRuntime r=fresh();int rc;rect(&r,60,60,80,160);pstr(&r,TITLE,"x");newc(&r,STORAGE,RECT,TITLE,1,0,0,1,bad[i],0,&rc);CHECK(rc==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
     static const unsigned good[]={0,1,2,8,9,10,16,24};
     for(unsigned i=0;i<sizeof good/sizeof *good;i++){MacRuntime r=fresh();int rc;rect(&r,60,60,80,160);pstr(&r,TITLE,"x");CHECK(newc(&r,STORAGE,RECT,TITLE,1,0,0,1,good[i],0,&rc)&&rc==1);mac_cpu_destroy(r.cpu);}}
    /* Corrupted guest input: each case halts, nothing is written to the screen. */
    for(unsigned k=0;k<16;k++){
        MacRuntime r=fresh();uint32_t h=mk(&r,60,60,80,160,0,0,10,0,"OK"),p=P(&r,h);int rc=0;
        switch(k){
        case 0:rc=h_word(&r,0xa963,0,1);break;                                                               /* nil handle */
        case 1:rc=h_word(&r,0xa963,h+1,1);break;                                                             /* odd handle */
        case 2:rc=h_word(&r,0xa963,0x4800,1);break;                                                          /* not a control handle */
        case 3:rc=h_word(&r,0xa963,0xfffffffc,1);break;                                                      /* beyond RAM */
        case 4:memcpy(mac_cpu_ram(r.cpu)+p+0x400,mac_cpu_ram(r.cpu)+p,CTL_BYTES);wr(&r,h,4,p+0x400);rc=h_word(&r,0xa963,h,1);break;   /* master pointer moved to a valid copy */
        case 5:wr(&r,p+CTL_OWNER,4,0x1234);rc=h_word(&r,0xa963,h,1);break;                                   /* owner not a window */
        case 6:rect(&r,60,60,80,160);pstr(&r,TITLE,"x");newc(&r,0x1234,RECT,TITLE,1,0,0,1,0,0,&rc);break;   /* window pointer */
        case 7:pstr(&r,TITLE,"x");newc(&r,STORAGE,0xfffffff8,TITLE,1,0,0,1,0,0,&rc);break;                  /* rectangle beyond RAM */
        case 8:rect(&r,60,60,80,160);newc(&r,STORAGE,RECT,0xfffffff0,1,0,0,1,0,0,&rc);break;               /* title beyond RAM */
        case 9:rect(&r,60,60,80,160);pstr(&r,TITLE,"x");newc(&r,STORAGE,RECT,TITLE,1,0,5,2,0,0,&rc);break;  /* max below min */
        case 10:mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-2);rc=mac_controls_service(&r,0xa960);break;   /* the stack does not fit */
        case 11:rc=h_long(&r,0xa95e,h,0xfffffff0);break;                                                     /* GetCTitle destination beyond RAM */
        case 12:rc=h_long(&r,0xa95f,h,0xfffffff0);break;                                                     /* SetCTitle source beyond RAM */
        case 13:rc=h_two(&r,0xa95c,0,10,10);break;                                                           /* SizeControl of a nil handle */
        case 14:wr(&r,SP+8,4,((uint32_t)5<<16)|5);wr(&r,SP+4,4,STORAGE);wr(&r,SP,4,0xfffffffc);rc=call(&r,0xa96c,12);break;   /* FindControl result pointer beyond RAM */
        default:wr(&r,p+CTL_NEXT,4,h);wr(&r,SP,4,STORAGE);rc=call(&r,0xa969,4);break;}                          /* a cycle in the control list must not loop forever: it halts */
        CHECK(rc==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    }
    /* A handle of another kind that the runtime owns is not a control, even with a valid-looking record behind it. */
    {MacRuntime r=fresh();uint32_t h=mk(&r,60,60,80,160,0,0,10,0,"OK"),p=P(&r,h);
     r.handles[r.handle_count++]=(MacHandle){.handle=0x4800,.pointer=p,.size=CTL_BYTES,.type=0x1234};wr(&r,0x4800,4,p);
     CHECK(h_word(&r,0xa963,0x4800,1)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    /* DrawGrowIcon: draws nothing for the window kinds this runtime has (none has a grow box), pops its argument, and halts for an unregistered window. */
    {MacRuntime r=fresh();int before=screen_sum(&r,430,280,450,300);wr(&r,SP,4,STORAGE);CHECK(call(&r,0xa904,4)==1 && screen_sum(&r,430,280,450,300)==before);
     wr(&r,SP,4,0x1234);CHECK(call(&r,0xa904,4)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);}
    mac_cpu_destroy(rt.cpu);
    puts("Control Manager checks passed");return 0;
}
