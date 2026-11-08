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
static void new_args(MacRuntime *rt,uint32_t storage,int visible) {
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,0x12345678);wr(rt,SP+4,2,0x01aa);
    wr(rt,SP+6,4,0xffffffff);wr(rt,SP+10,2,12);wr(rt,SP+12,2,visible?0x0100:0x00ff);
    wr(rt,SP+14,4,0x4001);wr(rt,SP+18,4,0x4100);wr(rt,SP+22,4,storage);
    memcpy(mac_cpu_ram(rt->cpu)+0x4001,"\015Monkey Island",14);
    wr(rt,0x4100,2,40);wr(rt,0x4102,2,50);wr(rt,0x4104,2,300);wr(rt,0x4106,2,450);
}
static int call(MacRuntime *rt,uint16_t op,uint32_t w){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,w);return mac_window_service(rt,op);}
static int show_hide(MacRuntime *rt,uint32_t w,int show){
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,2,show?0x0100:0);wr(rt,SP+2,4,w);return mac_window_service(rt,0xa908);
}
/* ShowHide ($A908): changes visibility like ShowWindow/HideWindow, but never activates the window or queues an event. */
static void show_hide_checks(void) {
    MacRuntime rt=fresh();new_args(&rt,STORAGE,0);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(rd(&rt,STORAGE+110,1)==0 && mac_cpu_ram(rt.cpu)[rt.screen_base+20*640+49]==77 && rt.event_count==0);
    CHECK(show_hide(&rt,STORAGE,1)==1 && mac_cpu_get(rt.cpu,MAC_A7)==SP+6);
    CHECK(rd(&rt,STORAGE+110,1)==1 && rd(&rt,STORAGE+111,1)==0);                       /* visible, not active */
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+20*640+49]==255 && rt.event_count==0);    /* frame drawn, no activate event */
    CHECK(show_hide(&rt,STORAGE,1)==1 && rd(&rt,STORAGE+110,1)==1 && rt.event_count==0); /* showing a shown window changes nothing */
    CHECK(show_hide(&rt,STORAGE,0)==1 && rd(&rt,STORAGE+110,1)==0);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+20*640+49]==77 && rt.event_count==0);     /* backdrop restored, still no event */
    CHECK(show_hide(&rt,STORAGE,0)==1 && rd(&rt,STORAGE+110,1)==0);
    /* The flag is a Boolean byte: any nonzero high byte shows. ShowWindow, by contrast, activates and queues an event. */
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,2,0x0200);wr(&rt,SP+2,4,STORAGE);CHECK(mac_window_service(&rt,0xa908)==1 && rd(&rt,STORAGE+110,1)==1);
    CHECK(show_hide(&rt,STORAGE,0)==1);
    CHECK(call(&rt,0xa915,STORAGE)==1 && rd(&rt,STORAGE+111,1)==1 && rt.event_count==1);
    /* The Boolean is the high byte of its word: garbage in the low byte does not make a hide into a show. */
    CHECK(show_hide(&rt,STORAGE,1)==1 && rd(&rt,STORAGE+110,1)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,2,0x0001);wr(&rt,SP+2,4,STORAGE);CHECK(mac_window_service(&rt,0xa908)==1 && rd(&rt,STORAGE+110,1)==0);
    /* Not inside BeginUpdate. */
    CHECK(show_hide(&rt,STORAGE,1)==1 && call(&rt,0xa922,STORAGE)==1);
    CHECK(show_hide(&rt,STORAGE,0)==-1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    /* A second window may be shown over a visible one when it is frontmost in the WindowList; the screen is restored in stacking order. */
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    {const uint8_t *scr=mac_cpu_ram(rt.cpu)+rt.screen_base;static uint8_t before[307200];memcpy(before,scr,307200);
     new_args(&rt,STORAGE+0x400,0);CHECK(mac_window_service(&rt,0xaa45)==1);
     CHECK(show_hide(&rt,STORAGE+0x400,1)==1 && mac_window_front(&rt)==STORAGE+0x400 && rd(&rt,STORAGE+110,1)==1 && rd(&rt,STORAGE+0x400+110,1)==1);   /* both visible */
     static uint8_t covered[307200];memcpy(covered,scr,307200);
     CHECK(memcmp(covered,before,307200)!=0);                                                                                                            /* the new window drew over the screen */
     /* hiding the covered window (same frame as the front one: nothing on screen changes) is allowed, and hiding the front one afterwards shows what was behind both */
     CHECK(show_hide(&rt,STORAGE,0)==1 && memcmp(scr,covered,307200)==0 && mac_window_front(&rt)==STORAGE+0x400 && rd(&rt,STORAGE+110,1)==0);
     {static uint8_t blank[307200];memset(blank,77,sizeof blank);CHECK(show_hide(&rt,STORAGE+0x400,0)==1 && memcmp(scr,blank,307200)==0 && mac_window_front(&rt)==0);}}
    mac_cpu_destroy(rt.cpu);
    /* a covered window that sticks out from under the front one cannot be hidden without compositing */
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    new_args(&rt,STORAGE+0x400,1);wr(&rt,0x4100,2,100);wr(&rt,0x4102,2,100);wr(&rt,0x4104,2,200);wr(&rt,0x4106,2,300);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(show_hide(&rt,STORAGE,0)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    /* the same rule for closing: a window wholly under the front one may be closed, leaving the screen alone */
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    {const uint8_t *scr=mac_cpu_ram(rt.cpu)+rt.screen_base;static uint8_t before[307200];memcpy(before,scr,307200);
     new_args(&rt,STORAGE+0x400,1);CHECK(mac_window_service(&rt,0xaa45)==1);static uint8_t covered[307200];memcpy(covered,scr,307200);
     mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);CHECK(mac_window_service(&rt,0xa92d)==1 && memcmp(scr,covered,307200)==0 && mac_window_front(&rt)==STORAGE+0x400);
     {static uint8_t blank[307200];memset(blank,77,sizeof blank);CHECK(show_hide(&rt,STORAGE+0x400,0)==1 && memcmp(scr,blank,307200)==0);}}
    mac_cpu_destroy(rt.cpu);
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    {const uint8_t *scr=mac_cpu_ram(rt.cpu)+rt.screen_base;static uint8_t before[307200];memcpy(before,scr,307200);
     new_args(&rt,STORAGE+0x400,1);CHECK(mac_window_service(&rt,0xaa45)==1 && mac_window_front(&rt)==STORAGE+0x400);                                      /* created visible in front */
     CHECK(show_hide(&rt,STORAGE+0x400,0)==1 && mac_window_front(&rt)==STORAGE && memcmp(scr,before,307200)==0);}                                       /* hiding the front one restores the screen exactly */
    mac_cpu_destroy(rt.cpu);
    /* A window that is not first in the WindowList cannot be shown over a visible one; neither can one created behind it; nor can a covered one be closed. */
    rt=fresh();new_args(&rt,STORAGE,0);CHECK(mac_window_service(&rt,0xaa45)==1);new_args(&rt,STORAGE+0x400,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(show_hide(&rt,STORAGE,1)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);new_args(&rt,STORAGE+0x400,1);wr(&rt,SP+6,4,0);
    CHECK(mac_window_service(&rt,0xaa45)==-1 && mac_cpu_halted(rt.cpu));mac_cpu_destroy(rt.cpu);
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);new_args(&rt,STORAGE+0x400,1);wr(&rt,0x4100,2,100);wr(&rt,0x4102,2,100);wr(&rt,0x4104,2,200);wr(&rt,0x4106,2,300);CHECK(mac_window_service(&rt,0xaa45)==1);
    {mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,STORAGE);CHECK(mac_window_service(&rt,0xa92d)==-1 && mac_cpu_halted(rt.cpu));}mac_cpu_destroy(rt.cpu);
    /* An unknown window halts before anything changes. */
    rt=fresh();new_args(&rt,STORAGE,0);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(show_hide(&rt,STORAGE+0x400,0)==-1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
}
static void lifecycle(void) {
    MacRuntime rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(rt.window.address==STORAGE && rd(&rt,SP+26,4)==STORAGE && mac_cpu_get(rt.cpu,MAC_A7)==SP+26);
    CHECK(rd(&rt,0x9d6,4)==STORAGE && rt.current_port==STORAGE);
    CHECK(rd(&rt,STORAGE+108,2)==8 && rd(&rt,STORAGE+110,4)==0x01010101);
    CHECK(rd(&rt,STORAGE+152,4)==0x12345678 && rd(&rt,STORAGE+144,4)==0);
    CHECK(rd(&rt,STORAGE+126,4)==0); /* No fabricated executable WDEF. */
    CHECK(call(&rt,0xa80a,STORAGE)==1 && rd(&rt,SP+4,2)==12);
    CHECK(rd(&rt,STORAGE+16,4)==0 && rd(&rt,STORAGE+20,4)==0x01040190);
    uint32_t pm=rd(&rt,rd(&rt,STORAGE+2,4),4),vis=rd(&rt,rt.window.handles[2],4),up=rd(&rt,rt.window.handles[10],4);
    CHECK((int16_t)rd(&rt,pm+6,2)==-40 && (int16_t)rd(&rt,pm+8,2)==-50);
    CHECK(rd(&rt,vis+6,2)==260 && rd(&rt,vis+8,2)==400);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+40*640+50]==0);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+20*640+49]==255);
    CHECK(mac_window_hit(&rt,100,100)==3 && mac_window_hit(&rt,57,27)==6 && mac_window_hit(&rt,437,27)==8);
    CHECK(mac_window_hit(&rt,100,27)==4 && mac_window_hit(&rt,10,10)==0);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x4300);wr(&rt,SP+4,2,100);wr(&rt,SP+6,2,100);
    CHECK(mac_window_service(&rt,0xa92c)==1 && rd(&rt,0x4300,4)==STORAGE && rd(&rt,SP+8,2)==3);
    CHECK(rt.event_count==1 && rt.events[rt.event_head].what==8 && rt.events[rt.event_head].message==STORAGE);
    CHECK(rt.modifiers==0x80 && (rt.events[rt.event_head].modifiers&1));
    rt.mouse_x=70;rt.mouse_y=55;mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x4400);
    CHECK(mac_event_service(&rt,0xa972)==1 && rd(&rt,0x4400,2)==15 && rd(&rt,0x4402,2)==20);
    MacEvent e;CHECK(mac_window_update_event(&rt,1u<<6,&e)==1 && e.message==STORAGE);
    CHECK(mac_window_update_event(&rt,1u<<6,&e)==1); /* Persistent until BeginUpdate. */
    for(unsigned n=0;n<2;n++) {
        mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x4200);wr(&rt,SP+4,2,1u<<6);
        CHECK(mac_event_service(&rt,0xa970)==1 && rd(&rt,SP+6,1)==1);
        CHECK(rd(&rt,0x4200,2)==6 && rd(&rt,0x4202,4)==STORAGE);
    }
    wr(&rt,up+2,2,60);wr(&rt,up+4,2,80);wr(&rt,up+6,2,120);wr(&rt,up+8,2,180);
    CHECK(call(&rt,0xa922,STORAGE)==1);
    CHECK(rd(&rt,vis+2,2)==20 && rd(&rt,vis+4,2)==30 && rd(&rt,vis+6,2)==80 && rd(&rt,vis+8,2)==130);
    CHECK(mac_window_update_event(&rt,1u<<6,&e)==0 && rd(&rt,up+2,4)==0);
    CHECK(call(&rt,0xa923,STORAGE)==1 && rd(&rt,vis+6,2)==260);
    CHECK(call(&rt,0xa917,STORAGE)==1 && rd(&rt,SP+4,4)==0x12345678);
    CHECK(call(&rt,0xa916,STORAGE)==1 && !rt.window.visible && !rt.window.active);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+40*640+50]==77 && mac_cpu_ram(rt.cpu)[rt.screen_base+20*640+49]==77);
    CHECK(call(&rt,0xa924,0)==1 && rd(&rt,SP,4)==0);
    CHECK(call(&rt,0xa915,STORAGE)==1 && mac_window_update_event(&rt,1u<<6,&e)==1);
    CHECK(call(&rt,0xa924,0)==1 && rd(&rt,SP,4)==STORAGE);
    CHECK(call(&rt,0xa92d,STORAGE)==1 && !rt.window.address && !rd(&rt,0x9d6,4));
    CHECK(rt.current_port==rt.wm_port && rt.event_count==0);
    CHECK(mac_cpu_ram(rt.cpu)[rt.screen_base+40*640+50]==77);
    mac_cpu_destroy(rt.cpu);
}
static void failures(void) {
    for(int kind=0;kind<12;kind++) {
        MacRuntime rt=fresh();new_args(&rt,STORAGE,1);
        if(kind==0)wr(&rt,SP+10,2,99);
        if(kind==1)wr(&rt,SP+18,4,MAC_RUNTIME_RAM-6);
        if(kind==2)wr(&rt,SP+14,4,MAC_RUNTIME_RAM-2),wr(&rt,MAC_RUNTIME_RAM-2,1,13);
        if(kind==3)wr(&rt,SP+22,4,SP);
        if(kind==4)wr(&rt,0x4104,2,481);
        if(kind==5)rt.heap_limit=rt.heap_next+100;
        if(kind==6)wr(&rt,SP+22,4,rd(&rt,rt.screen_colors,4));
        if(kind==7)wr(&rt,SP+22,4,0x9d6);
        if(kind==8)rt.event_count=64;
        if(kind==9)rt.heap_high=rt.heap_next+100;
        if(kind==10){rt.heap_next=MAC_RUNTIME_RAM-100;rt.heap_limit=UINT32_MAX;}
        if(kind==11)wr(&rt,SP+22,4,rt.heap_next+128);
        uint32_t heap=rt.heap_next;size_t count=rt.handle_count;
        memset(mac_cpu_ram(rt.cpu)+STORAGE,0x5a,156);
        CHECK(mac_window_service(&rt,0xaa45)==(kind==5 || kind==9 || kind==10?1:-1));
        CHECK(rt.heap_next==heap && rt.handle_count==count && !rt.window.address);
        CHECK(mac_cpu_ram(rt.cpu)[STORAGE]==0x5a && mac_cpu_ram(rt.cpu)[STORAGE+155]==0x5a);
        CHECK(mac_cpu_ram(rt.cpu)[0x9d6]==0 && mac_cpu_ram(rt.cpu)[rt.screen_base+40*640+50]==77);
        mac_cpu_destroy(rt.cpu);
    }
    MacRuntime rt=fresh();new_args(&rt,0,0);CHECK(mac_window_service(&rt,0xaa45)==1);
    uint32_t w=rt.window.address;CHECK(w && rt.window.owns_storage && !rt.window.visible);
    CHECK(call(&rt,0xa914,w)==1 && !rt.window.address);mac_cpu_destroy(rt.cpu);
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    CHECK(call(&rt,0xa914,STORAGE)==-1 && rt.window.address==STORAGE);mac_cpu_destroy(rt.cpu);
    rt=fresh();new_args(&rt,STORAGE,1);CHECK(mac_window_service(&rt,0xaa45)==1);
    new_args(&rt,STORAGE+512,1);                                                  /* a second visible window in front is now allowed (stacking order) */
    CHECK(mac_window_service(&rt,0xaa45)==1 && mac_window_front(&rt)==STORAGE+512);
    mac_cpu_destroy(rt.cpu);
}
/* CalcVisBehind(startWindow, clobberedRgn) recomputes the one window's visRgn like CalcVis; a NIL start does nothing; the region is only popped. */
static void calc_vis_behind(void) {
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 uint32_t vis=rd(&r,r.window.handles[2],4);wr(&r,vis+2,4,0);wr(&r,vis+6,4,0);
 mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0x1234);wr(&r,SP+4,4,STORAGE);
 CHECK(mac_window_service(&r,0xa90a)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+8 && rd(&r,vis+6,2)==260 && rd(&r,vis+8,2)==400);
 wr(&r,vis+2,4,0);wr(&r,vis+6,4,0);mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0x1234);wr(&r,SP+4,4,0);
 CHECK(mac_window_service(&r,0xa90a)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+8 && rd(&r,vis+6,2)==0);
 mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0x1234);wr(&r,SP+4,4,0x5555);
 CHECK(mac_window_service(&r,0xa90a)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0);wr(&r,SP+4,4,STORAGE);
 CHECK(call(&r,0xa916,STORAGE)==1);mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0);wr(&r,SP+4,4,STORAGE);
 vis=rd(&r,r.window.handles[2],4);CHECK(mac_window_service(&r,0xa90a)==1 && rd(&r,vis+2,4)==0 && rd(&r,vis+6,4)==0);mac_cpu_destroy(r.cpu);   /* hidden: an empty vis, as CalcVis */
}
static void calc_vis(void) {
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 uint32_t vis=rd(&r,r.window.handles[2],4),content=rd(&r,r.window.handles[9],4),update=rd(&r,r.window.handles[10],4);
 uint8_t saved[10];memcpy(saved,mac_cpu_ram(r.cpu)+update,10);
 wr(&r,vis+2,4,0);wr(&r,vis+6,4,0);uint32_t port=r.current_port;unsigned events=r.event_count;
 CHECK(call(&r,0xa909,STORAGE)==1 && rd(&r,vis+6,2)==260 && rd(&r,vis+8,2)==400);
 CHECK(r.current_port==port && r.event_count==events && !memcmp(saved,mac_cpu_ram(r.cpu)+update,10));
 /* Narrow global content clips then translates to the local window space. */
 wr(&r,content+2,2,50);wr(&r,content+4,2,70);wr(&r,content+6,2,100);wr(&r,content+8,2,200);
 CHECK(call(&r,0xa909,STORAGE)==1 && rd(&r,vis+2,2)==10 && rd(&r,vis+4,2)==20 && rd(&r,vis+6,2)==60 && rd(&r,vis+8,2)==150);
 CHECK(call(&r,0xa916,STORAGE)==1);CHECK(call(&r,0xa909,STORAGE)==1 && rd(&r,vis+2,4)==0 && rd(&r,vis+6,4)==0);
 mac_cpu_destroy(r.cpu);
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);CHECK(call(&r,0xa922,STORAGE)==1);
 vis=rd(&r,r.window.handles[2],4);memcpy(saved,mac_cpu_ram(r.cpu)+vis,10);
 CHECK(call(&r,0xa909,STORAGE)==-1 && !memcmp(saved,mac_cpu_ram(r.cpu)+vis,10));mac_cpu_destroy(r.cpu);
}
static int resize(MacRuntime *r,int width,int height,int update) {
 mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,2,update?0x0100:0);wr(r,SP+2,2,height);wr(r,SP+4,2,width);wr(r,SP+6,4,STORAGE);
 return mac_window_service(r,0xa91d);
}
static void sizing(void) {
 MacRuntime r=fresh();new_args(&r,STORAGE,0);CHECK(mac_window_service(&r,0xaa45)==1);
 uint32_t vis=rd(&r,r.window.handles[2],4),up=rd(&r,r.window.handles[10],4),content=rd(&r,r.window.handles[9],4);
 uint32_t port=r.current_port,heap=r.heap_next;unsigned events=r.event_count;
 CHECK(resize(&r,384,198,0)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
 CHECK(r.window.bounds[2]==238 && r.window.bounds[3]==434 && rd(&r,STORAGE+20,2)==198 && rd(&r,STORAGE+22,2)==384);
 CHECK(rd(&r,content+6,2)==238 && rd(&r,content+8,2)==434 && rd(&r,vis+6,4)==0 && rd(&r,up+6,4)==0);
 CHECK(r.current_port==port && r.heap_next==heap && r.event_count==events && mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]==77);
 CHECK(resize(&r,0,0,1)==1 && r.window.bounds[2]==238);
 mac_cpu_destroy(r.cpu);
 for(unsigned k=0;k<6;k++) {                                  /* larger than the screen, or smaller than the minimum: refused atomically */
  r=fresh();new_args(&r,STORAGE,0);CHECK(mac_window_service(&r,0xaa45)==1);
  CHECK(resize(&r,k==0?700:k==1?600:k==2?63:k==3?100:k==4?641:640,k==0?470:k==1?490:k==2?100:k==3?31:k==4?100:481,0)==-1 && mac_cpu_halted(r.cpu) && r.window.bounds[2]==300 && r.window.bounds[3]==450);
  mac_cpu_destroy(r.cpu);
 }
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]=42;
 CHECK(resize(&r,450,300,1)==1);up=rd(&r,r.window.handles[10],4);
 CHECK(rd(&r,up+2,2)==40 && rd(&r,up+4,2)==50 && rd(&r,up+6,2)==340 && rd(&r,up+8,2)==500);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]==42 && mac_cpu_ram(r.cpu)[r.screen_base+330*640+490]==0);
 CHECK(resize(&r,200,100,0)==1);CHECK(mac_cpu_ram(r.cpu)[r.screen_base+330*640+490]==77);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]==42);mac_cpu_destroy(r.cpu);
 /* An empty old update plus growth on both axes is L-shaped: fail atomically. */
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);up=rd(&r,r.window.handles[10],4);
 wr(&r,up+2,4,0);wr(&r,up+6,4,0);uint32_t pixels=rd(&r,r.screen_base+100*640+100,4);
 CHECK(resize(&r,450,300,1)==-1 && r.window.bounds[2]==300 && r.window.bounds[3]==450);
 CHECK(!memcmp(mac_cpu_ram(r.cpu)+up+2,"\0\0\0\0\0\0\0\0",8));
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]==(pixels>>24));mac_cpu_destroy(r.cpu);
}
static int move_to(MacRuntime *r,int x,int y,int front) {
 mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,2,front?0x0100:0);wr(r,SP+2,2,y);wr(r,SP+4,2,x);wr(r,SP+6,4,STORAGE);return mac_window_service(r,0xa91b);
}
static void moving(void) {
 for(unsigned visible=0;visible<2;visible++) {
  MacRuntime r=fresh();new_args(&r,STORAGE,(int)visible);CHECK(mac_window_service(&r,0xaa45)==1);
  CHECK(resize(&r,384,198,0)==1);uint32_t pm=rd(&r,r.window.handles[0],4),vis=rd(&r,r.window.handles[2],4),up=rd(&r,r.window.handles[10],4);
  uint8_t portrect[8],oldvis[10];memcpy(portrect,mac_cpu_ram(r.cpu)+STORAGE+16,8);memcpy(oldvis,mac_cpu_ram(r.cpu)+vis,10);
  if(visible)mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]=42;
  unsigned events=r.event_count;uint32_t port=r.current_port;
  CHECK(move_to(&r,128,141,0)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
  CHECK(r.window.bounds[0]==141 && r.window.bounds[1]==128 && r.window.bounds[2]==339 && r.window.bounds[3]==512);
  CHECK((int16_t)rd(&r,pm+6,2)==-141 && (int16_t)rd(&r,pm+8,2)==-128);
  CHECK(!memcmp(portrect,mac_cpu_ram(r.cpu)+STORAGE+16,8) && !memcmp(oldvis,mac_cpu_ram(r.cpu)+vis,10));
  CHECK(r.current_port==port && r.event_count==events);
  if(visible){CHECK(mac_cpu_ram(r.cpu)[r.screen_base+201*640+178]==42);CHECK(mac_cpu_ram(r.cpu)[r.screen_base+100*640+100]==77);CHECK(rd(&r,up+2,2)==141 && rd(&r,up+4,2)==128);}
  else CHECK(rd(&r,up+2,4)==0 && rd(&r,up+6,4)==0);
  CHECK(move_to(&r,-1,141,0)==-1 && r.window.bounds[1]==128);mac_cpu_destroy(r.cpu);
 }
}
/* A window may extend past the bottom or right edge of the screen while it is sized (a game can size first and move after). Nothing may
 * be written outside the screen buffer; moving it back on-screen keeps the on-screen pixels and starts the rest as paper. */
static void offscreen(void) {
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 uint8_t *ram=mac_cpu_ram(r.cpu);uint32_t after=r.screen_base+640*480;
 unsigned light=0,max=0;for(unsigned i=0;i<256;i++){uint32_t c=r.palette[i];unsigned v=((c>>16)&255)+((c>>8)&255)+(c&255);if(v>=max){max=v;light=i;}}
 CHECK(light!=77 && light!=42);
 memset(ram+after,0x6b,0x40000);ram[r.screen_base+100*640+100]=42;
 CHECK(resize(&r,590,440,0)==1 && r.window.bounds[2]==480 && r.window.bounds[3]==640);                 /* exactly on-screen */
 CHECK(resize(&r,600,470,0)==1 && r.window.bounds[2]==510 && r.window.bounds[3]==650);                 /* past the bottom and right edges */
 for(unsigned i=0;i<0x40000;i++)if(ram[after+i]!=0x6b){CHECK(0);break;}
 CHECK(ram[r.screen_base+100*640+100]==42);
 CHECK(move_to(&r,0,10,0)==1 && r.window.bounds[0]==10 && r.window.bounds[1]==0 && r.window.bounds[2]==480 && r.window.bounds[3]==600);
 for(unsigned i=0;i<0x40000;i++)if(ram[after+i]!=0x6b){CHECK(0);break;}
 CHECK(ram[r.screen_base+70*640+50]==42);                                                              /* the on-screen pixel moved with the window */
 CHECK(ram[r.screen_base+460*640+100]==light && ram[r.screen_base+200*640+595]==light);               /* what was off-screen starts as paper */
 mac_cpu_destroy(r.cpu);
 /* moving a window that is off-screen to another off-screen place is still refused, atomically */
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);CHECK(resize(&r,600,470,0)==1);
 CHECK(move_to(&r,0,30,0)==-1 && r.window.bounds[0]==40 && r.window.bounds[2]==510);mac_cpu_destroy(r.cpu);
 /* growing a visible off-screen window with updating on is refused, atomically; with updating off, or hidden, it works */
 r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 CHECK(resize(&r,600,470,1)==-1 && r.window.bounds[2]==300 && r.window.bounds[3]==450);mac_cpu_destroy(r.cpu);
 r=fresh();new_args(&r,STORAGE,0);CHECK(mac_window_service(&r,0xaa45)==1);CHECK(resize(&r,600,470,1)==1 && r.window.bounds[2]==510);mac_cpu_destroy(r.cpu);
}
int main(void){calc_vis_behind();show_hide_checks();moving();sizing();offscreen();calc_vis();lifecycle();failures();puts("one-window lifecycle, drawing, and update checks passed");}
