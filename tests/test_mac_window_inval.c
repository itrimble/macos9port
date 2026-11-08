/* InvalRect / InvalRgn / ValidRect / ValidRgn on the window that owns the current port. */
#include "mac_window_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define STORAGE 0x700000u
#define RECT 0x4300u
#define REGION_H 0x20000u
#define REGION_P 0x20010u
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static MacRuntime fresh(void) {
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x100000;rt.heap_limit=0x600000;rt.qd_globals=0x7000;rt.modifiers=0x80;
    CHECK(mac_color_init(&rt)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_toolbox_service(&rt,0xa912)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x12345678);wr(&rt,SP+4,2,0x01aa);
    wr(&rt,SP+6,4,0xffffffff);wr(&rt,SP+10,2,12);wr(&rt,SP+12,2,0x0100);
    wr(&rt,SP+14,4,0x4001);wr(&rt,SP+18,4,0x4100);wr(&rt,SP+22,4,STORAGE);
    memcpy(mac_cpu_ram(rt.cpu)+0x4001,"\\004Test",5);
    wr(&rt,0x4100,2,40);wr(&rt,0x4102,2,50);wr(&rt,0x4104,2,300);wr(&rt,0x4106,2,450); /* content: top 40, left 50, bottom 300, right 450 */
    CHECK(mac_window_service(&rt,0xaa45)==1);
    return rt;
}
typedef struct{int t,l,b,r;}B;
static uint32_t update_ptr(MacRuntime *rt){return rd(rt,rt->window.handles[10],4);}
static void set_update(MacRuntime *rt,B b){uint32_t p=update_ptr(rt);wr(rt,p,2,10);wr(rt,p+2,2,(uint16_t)b.t);wr(rt,p+4,2,(uint16_t)b.l);wr(rt,p+6,2,(uint16_t)b.b);wr(rt,p+8,2,(uint16_t)b.r);}
static B update(MacRuntime *rt){uint32_t p=update_ptr(rt);return (B){(int16_t)rd(rt,p+2,2),(int16_t)rd(rt,p+4,2),(int16_t)rd(rt,p+6,2),(int16_t)rd(rt,p+8,2)};}
static int is(B a,int t,int l,int b,int r){return a.t==t && a.l==l && a.b==b && a.r==r;}
static int rect_call(MacRuntime *rt,unsigned op,int t,int l,int b,int r){
    wr(rt,RECT,2,(uint16_t)t);wr(rt,RECT+2,2,(uint16_t)l);wr(rt,RECT+4,2,(uint16_t)b);wr(rt,RECT+6,2,(uint16_t)r);
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,RECT);return mac_window_inval_service(rt,(uint16_t)op);
}
static void make_region(MacRuntime *rt,int size_word,int t,int l,int b,int r){
    wr(rt,REGION_H,4,REGION_P);wr(rt,REGION_P,2,(uint16_t)size_word);wr(rt,REGION_P+2,2,(uint16_t)t);wr(rt,REGION_P+4,2,(uint16_t)l);wr(rt,REGION_P+6,2,(uint16_t)b);wr(rt,REGION_P+8,2,(uint16_t)r);
}
static int region_call(MacRuntime *rt,unsigned op,uint32_t h){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,h);return mac_window_inval_service(rt,(uint16_t)op);}

int main(void) {
    enum{INVAL=0xa928,VALID=0xa92a,INVAL_RGN=0xa927,VALID_RGN=0xa929};
    MacRuntime rt=fresh();
    CHECK(rt.current_port==STORAGE);
    /* A shown window starts with its whole content to update (global coordinates). */
    CHECK(is(update(&rt),40,50,300,450));
    set_update(&rt,(B){0,0,0,0});
    CHECK(mac_window_inval_service(&rt,0xa926)==0 && mac_window_inval_service(&rt,0xa92b)==0);            /* neighbours are not ours */
    /* InvalRect: local to global, popped argument, an empty update takes the rectangle. */
    CHECK(rect_call(&rt,INVAL,0,0,10,10)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && is(update(&rt),40,50,50,60));
    /* A rectangle that joins exactly is an exact union; one that does not grows to the bounding box. */
    CHECK(rect_call(&rt,INVAL,0,10,10,20)==1 && is(update(&rt),40,50,50,70));
    CHECK(rect_call(&rt,INVAL,100,100,110,110)==1 && is(update(&rt),40,50,150,160));
    CHECK(rect_call(&rt,INVAL,5,5,6,6)==1 && is(update(&rt),40,50,150,160));                               /* already inside */
    /* Clipped to the content; entirely outside changes nothing; an empty rectangle changes nothing. */
    set_update(&rt,(B){0,0,0,0});
    CHECK(rect_call(&rt,INVAL,-50,-50,5,5)==1 && is(update(&rt),40,50,45,55));
    set_update(&rt,(B){0,0,0,0});
    CHECK(rect_call(&rt,INVAL,400,400,410,410)==1 && is(update(&rt),0,0,0,0));
    CHECK(rect_call(&rt,INVAL,5,5,5,50)==1 && is(update(&rt),0,0,0,0));
    CHECK(rect_call(&rt,INVAL,0,0,10,10)==1);
    CHECK(rect_call(&rt,INVAL,-1000,-1000,1000,1000)==1 && is(update(&rt),40,50,300,450));                 /* never beyond the content */
    /* ValidRect: all of it, a full-width strip, a full-height strip, and the cases that stay conservative. */
    CHECK(rect_call(&rt,VALID,0,0,260,400)==1 && is(update(&rt),0,0,0,0));
    set_update(&rt,(B){40,50,100,100});
    CHECK(rect_call(&rt,VALID,0,0,10,50)==1 && is(update(&rt),50,50,100,100));                             /* top strip removed */
    set_update(&rt,(B){40,50,100,100});
    CHECK(rect_call(&rt,VALID,30,0,100,50)==1 && is(update(&rt),40,50,70,100));                            /* bottom strip removed */
    set_update(&rt,(B){40,50,100,100});
    CHECK(rect_call(&rt,VALID,0,0,100,10)==1 && is(update(&rt),40,60,100,100));                            /* left strip removed */
    set_update(&rt,(B){40,50,100,100});
    CHECK(rect_call(&rt,VALID,0,30,100,100)==1 && is(update(&rt),40,50,100,80));                           /* right strip removed */
    set_update(&rt,(B){40,50,100,100});
    CHECK(rect_call(&rt,VALID,10,10,20,20)==1 && is(update(&rt),40,50,100,100));                           /* a hole: left as it was */
    CHECK(rect_call(&rt,VALID,10,10,100,20)==1 && is(update(&rt),40,50,100,100));                          /* a notch: left as it was */
    CHECK(rect_call(&rt,VALID,150,150,160,160)==1 && is(update(&rt),40,50,100,100));                       /* disjoint */
    CHECK(rect_call(&rt,VALID,5,5,5,50)==1 && is(update(&rt),40,50,100,100));                              /* empty rectangle */
    set_update(&rt,(B){0,0,0,0});CHECK(rect_call(&rt,VALID,0,0,50,50)==1 && is(update(&rt),0,0,0,0));      /* nothing to remove */
    /* The port's pixmap bounds are the origin: moving them moves the global position. */
    {uint32_t pm=rd(&rt,rd(&rt,STORAGE+2,4),4);wr(&rt,pm+6,2,(uint16_t)-60);wr(&rt,pm+8,2,(uint16_t)-70);
     CHECK(rect_call(&rt,INVAL,0,0,10,10)==1 && is(update(&rt),60,70,70,80));
     wr(&rt,pm+6,2,(uint16_t)-40);wr(&rt,pm+8,2,(uint16_t)-50);}
    /* The region forms behave like the rectangle forms, with the region read in local coordinates. */
    set_update(&rt,(B){0,0,0,0});make_region(&rt,10,0,0,10,10);
    CHECK(region_call(&rt,INVAL_RGN,REGION_H)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+4 && is(update(&rt),40,50,50,60));
    make_region(&rt,10,0,0,5,60);CHECK(region_call(&rt,VALID_RGN,REGION_H)==1 && is(update(&rt),45,50,50,60));
    /* Refusals halt with a message and change nothing. */
    for(unsigned k=0;k<10;k++){
        MacRuntime r2=fresh();set_update(&r2,(B){40,50,100,100});
        int rc;
        if(k==0){make_region(&r2,22,0,0,10,10);rc=region_call(&r2,INVAL_RGN,REGION_H);}                       /* a complex region */
        else if(k==1)rc=region_call(&r2,VALID_RGN,0x5000);                                                    /* not a region */
        else if(k==2)rc=region_call(&r2,INVAL_RGN,0);
        else if(k==3){r2.current_port=r2.wm_port;rc=rect_call(&r2,INVAL,0,0,10,10);}                          /* not a window */
        else if(k==4){r2.current_port=0x1234;rc=rect_call(&r2,INVAL,0,0,10,10);}                              /* not a registered port */
        else if(k==5){wr(&r2,STORAGE+6,2,0x0004);rc=rect_call(&r2,INVAL,0,0,10,10);}                          /* a basic (non-color) port */
        else if(k==6){mac_cpu_set(r2.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r2.cpu)-2);rc=mac_window_inval_service(&r2,INVAL);}
        else if(k==7){wr(&r2,RECT,2,0);mac_cpu_set(r2.cpu,MAC_A7,SP);wr(&r2,SP,4,(uint32_t)mac_cpu_ram_size(r2.cpu)-4);rc=mac_window_inval_service(&r2,INVAL);} /* rectangle beyond RAM */
        else if(k==8){wr(&r2,0x5006,2,0xc000);wr(&r2,0x5002,4,0x5100);wr(&r2,0x5100,4,0x5200);r2.current_port=0x5000;rc=rect_call(&r2,INVAL,0,0,10,10);}      /* a color port that is not a window (offscreen) */
        else{wr(&r2,update_ptr(&r2),2,22);rc=rect_call(&r2,INVAL,0,0,10,10);}                                  /* the window's own update region is corrupt */
        CHECK(rc==-1 && mac_cpu_halted(r2.cpu));
        {const uint8_t *ram=mac_cpu_ram(r2.cpu);uint32_t h=r2.window.handles[10];uint32_t p=(uint32_t)(ram[h]<<24|ram[h+1]<<16|ram[h+2]<<8|ram[h+3]);
         CHECK(ram[p+3]==40 && ram[p+5]==50 && ram[p+7]==100 && ram[p+9]==100);}
        mac_cpu_destroy(r2.cpu);
    }
    mac_cpu_destroy(rt.cpu);
    puts("InvalRect/ValidRect checks passed");return 0;
}
