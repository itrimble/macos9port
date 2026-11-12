/* Custom menu definition procedures on the 68K runtime: a synthetic MDEF (tests/mdef_fixture.h, hand-assembled 68K code) really runs, through
 * mac_guest_call and the CPU, for the size message (GetMenu, GetNewMBar, CalcMenuSize and before every MenuSelect/PopUpMenuSelect), the draw
 * message, the choose message while the button is down and the pop-up message. Menus with the standard definition must not change. */
#include "mac_menu_int.h"
#include "mac_mdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fails++;}}while(0)
#include "fork_builder.h"
#include "mdef_fixture.h"
#define SP 0x8000u
#define PC 0x5002u
#define SCR 0xe00000u
#define LOG 0x90000u
#define STUB 0x27f0u
static uint8_t fork_bytes[FB_BYTES];
static FbFork fork_def;
static size_t fork_len;
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static int hook(MacCpu*cpu,uint16_t op,uint32_t pc,void*u){
    MacRuntime*rt=u;(void)pc;
    if(op==0xa0fe && rt->guest.active)return mac_guest_return(rt);
    mac_cpu_halt(cpu,"unexpected trap in the MDEF test");return -1;
}
static void build_fork(void){
    uint8_t code[512],res[256];static const char*file_items[]={"Open","Close","Quit",0},*custom_items[]={"Alpha","Beta","Gamma",0},*edit_items[]={"Cut","Copy",0};
    size_t n=mdef_image(code);CHECK(n>0);
    fb_add(&fork_def,0x4d444546,128,0,code,n);                                          /* 'MDEF' 128 */
    n=mdef_menu_res(res,128,0,"File",file_items,"O Q");fb_add(&fork_def,0x4d454e55,128,0,res,n);
    n=mdef_menu_res(res,129,0,"Edit",edit_items,"XC");fb_add(&fork_def,0x4d454e55,129,0,res,n);
    n=mdef_menu_res(res,140,128,"Custom",custom_items,"a  ");fb_add(&fork_def,0x4d454e55,140,0,res,n);
    n=mdef_menu_res(res,141,128,"Pop",custom_items,0);fb_add(&fork_def,0x4d454e55,141,0,res,n);
    n=mdef_menu_res(res,142,999,"Lost",custom_items,0);fb_add(&fork_def,0x4d454e55,142,0,res,n);   /* names an MDEF the program does not have */
    static const uint8_t mbar[]={0,2,0,128,0,140};fb_add(&fork_def,0x4d424152,128,0,mbar,sizeof mbar);
    fork_len=fb_build(&fork_def,fork_bytes);
}
static void settle(MacRuntime*r){if(r->guest.active)mac_cpu_run(r->cpu,4000);}
static void rig(MacRuntime*r){
    memset(r,0,sizeof *r);r->cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r->cpu);r->heap_next=0x10000;r->heap_limit=0x80000;
    r->screen_base=SCR;r->screen_width=640;r->screen_height=480;r->screen_row_bytes=640;r->screen_depth=8;r->palette[0]=0xffffff;
    CHECK(mac_toolbox_service(r,0xa930)==1);wr(r,0xbaa,2,20);
    CHECK(!mac_resource_open(&r->fork,fork_bytes,fork_len,NULL,0));
    wr(r,STUB,2,0xa0fe);wr(r,PC,2,0x60fe);wr(r,0xa78,4,LOG);wr(r,0xa7c,4,0);
    mac_cpu_set_trap(r->cpu,hook,r);
}
static uint32_t lg(MacRuntime*r,int message,unsigned off,unsigned n){return rd(r,MDEF_SLOT(LOG,message)+off,n);}
static int16_t lgs(MacRuntime*r,int message,unsigned off){return (int16_t)lg(r,message,off,2);}
static int svc(MacRuntime*r,unsigned op){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);int rc=mac_menu_service(r,(uint16_t)op);settle(r);return rc;}
static uint32_t get_menu(MacRuntime*r,int id){wr(r,SP,2,(uint16_t)id);wr(r,SP+2,4,0xdeadbeef);CHECK(svc(r,0xa9bf)==1);return rd(r,SP+2,4);}
static void insert(MacRuntime*r,uint32_t h,int before){wr(r,SP,2,(uint16_t)before);wr(r,SP+2,4,h);CHECK(svc(r,0xa935)==1);}
static uint8_t *screen(MacRuntime*r){return mac_cpu_ram(r->cpu)+SCR;}
static unsigned px(MacRuntime*r,int x,int y){return screen(r)[y*640+x];}
static void marker(MacRuntime*r,int x,int y){wr(r,0xa7c,4,SCR+(unsigned)(y*640+x));}
static int msel(MacRuntime*r,int v,int h){wr(r,SP,2,(uint16_t)v);wr(r,SP+2,2,(uint16_t)h);wr(r,SP+4,4,0xdeadbeef);mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);int rc=mac_menuselect_service(r,0xa93d);settle(r);return rc;}
static int again(MacRuntime*r){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);int rc=mac_menuselect_service(r,0xa93d);settle(r);return rc;}
static int popup(MacRuntime*r,uint32_t menu,int top,int left,int item){
    wr(r,SP,2,(uint16_t)item);wr(r,SP+2,2,(uint16_t)left);wr(r,SP+4,2,(uint16_t)top);wr(r,SP+6,4,menu);wr(r,SP+10,4,0xdeadbeef);
    mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);int rc=mac_menuselect_service(r,0xa80b);settle(r);return rc;
}
static int popup_again(MacRuntime*r){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);int rc=mac_menuselect_service(r,0xa80b);settle(r);return rc;}
static void pointer(MacRuntime*r,int x,int y){r->mouse_x=(int16_t)x;r->mouse_y=(int16_t)y;}
static void release(MacRuntime*r,int x,int y){mac_runtime_post_event(r,2,0,0x80,(int16_t)x,(int16_t)y);}
static int rect_is(MacRuntime*r,int message,int t,int l,int b,int rr){return lgs(r,message,6)==t&&lgs(r,message,8)==l&&lgs(r,message,10)==b&&lgs(r,message,12)==rr;}
static int tracking_idle(MacRuntime*r){return r->menusel && !r->guest.active && mac_cpu_get(r->cpu,MAC_PC)==0x5000 && mac_cpu_get(r->cpu,MAC_A7)==SP;}

/* A bar with File (standard), Custom (MDEF 128) and Edit (standard). */
static void bar(MacRuntime*rp,uint32_t*custom){
    rig(rp);uint32_t f=get_menu(rp,128),c=get_menu(rp,140),e=get_menu(rp,129);
    insert(rp,f,0);insert(rp,c,0);insert(rp,e,0);CHECK(svc(rp,0xa937)==1);*custom=c;
}

static void sizing(void){
    MacRuntime r;rig(&r);
    /* GetMenu of a standard menu: no guest call, sizes from the items, no menuProc */
    uint32_t f=get_menu(&r,128);MacHandle*m=mac_menu_record(&r,f,NULL);
    CHECK(f && m && !rd(&r,m->pointer+6,4) && rd(&r,m->pointer+2,2)>0 && rd(&r,m->pointer+4,2)==48 && !r.guest.active && lg(&r,MDEF_SIZE,0,2)==0);
    /* GetMenu of a custom menu: the size message runs the 68K procedure; the result and the popped argument wait for it */
    wr(&r,SP,2,140);wr(&r,SP+2,4,0xdeadbeef);mac_cpu_set(r.cpu,MAC_A7,SP);mac_cpu_set(r.cpu,MAC_PC,PC);
    CHECK(mac_menu_service(&r,0xa9bf)==1 && r.guest.active && rd(&r,SP+2,4)==0xdeadbeef);
    uint32_t code=mac_cpu_get(r.cpu,MAC_PC);CHECK(code>=0x400 && !(code&1) && code!=PC);
    mac_cpu_run(r.cpu,4000);
    uint32_t c=rd(&r,SP+2,4);m=mac_menu_record(&r,c,NULL);
    CHECK(!r.guest.active && mac_cpu_get(r.cpu,MAC_A7)==SP+2 && c && m && c!=0xdeadbeef);
    CHECK(rd(&r,m->pointer+2,2)==MDEF_W && rd(&r,m->pointer+4,2)==MDEF_H);                       /* the MDEF's answer, not ours */
    uint32_t mh=rd(&r,m->pointer+6,4);
    CHECK(mh && mac_mdef_code(&r,mh)==code && mac_mdef_of_menu(&r,m)==mh);
    CHECK(lg(&r,MDEF_SIZE,0,2)==1 && lg(&r,MDEF_SIZE,14,4)==c && lgs(&r,MDEF_SIZE,18)==0 && rect_is(&r,MDEF_SIZE,0,0,0,0));
    CHECK(lg(&r,MDEF_DRAW,0,2)==0 && lg(&r,MDEF_CHOOSE,0,2)==0);
    { const MacHandle*d=NULL;for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==mh)d=&r.handles[i];CHECK(d && d->loaded_code && d->resource && d->type==0x4d444546);}
    unsigned n=0;CHECK(mac_menu_record(&r,c,&n) && n==3);
    /* GetMenu again gives the same menu (and sizes it again) */
    wr(&r,SP,2,140);CHECK(svc(&r,0xa9bf)==1 && rd(&r,SP+2,4)==c && lg(&r,MDEF_SIZE,0,2)==2);
    /* CalcMenuSize sends the size message again; the old size is replaced */
    wr(&r,m->pointer+2,2,0);wr(&r,m->pointer+4,2,0);wr(&r,SP,4,c);wr(&r,SP+4,4,0xdeadbeef);
    CHECK(svc(&r,0xa948)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4 && lg(&r,MDEF_SIZE,0,2)==3 && rd(&r,m->pointer+2,2)==MDEF_W && rd(&r,m->pointer+4,2)==MDEF_H);
    /* CalcMenuSize on a standard menu stays native */
    wr(&r,SP,4,f);CHECK(svc(&r,0xa948)==1 && lg(&r,MDEF_SIZE,0,2)==3);
    /* items can be added to a custom menu: the record keeps its menuProc and the count follows */
    memcpy(mac_cpu_ram(r.cpu)+0x4100,"\007Delta/d",8);wr(&r,SP,4,0x4100);wr(&r,SP+4,4,c);
    CHECK(svc(&r,0xa933)==1 && mac_menu_record(&r,c,&n) && n==4 && rd(&r,m->pointer+6,4)==mh && !r.guest.active);
    /* GetNewMBar: every custom menu in the list is sized before the list is delivered */
    wr(&r,SP,2,128);wr(&r,SP+2,4,0xdeadbeef);CHECK(svc(&r,0xa9c0)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2);
    { uint32_t lh=rd(&r,SP+2,4);CHECK(lh && lh!=0xdeadbeef && rd(&r,rd(&r,lh,4),2)==2 && lg(&r,MDEF_SIZE,0,2)==4);}
    mac_cpu_destroy(r.cpu);
}

static void bar_calls(void){
    uint32_t c;MacRuntime r;bar(&r,&c);
    /* DrawMenuBar draws titles itself and never asks the definition to draw; MenuKey reads the item data */
    CHECK(lg(&r,MDEF_DRAW,0,2)==0 && lg(&r,MDEF_CHOOSE,0,2)==0);
    wr(&r,SP,2,'A'<<8);wr(&r,SP+2,4,0xdeadbeef);mac_cpu_set(r.cpu,MAC_A7,SP);
    CHECK(mac_menuselect_service(&r,0xa93e)==1 && rd(&r,SP+2,4)==((140u<<16)|1) && r.menu_hilite==140);
    CHECK(lg(&r,MDEF_DRAW,0,2)==0 && lg(&r,MDEF_CHOOSE,0,2)==0);
    mac_cpu_destroy(r.cpu);
}

static void menu_select(void){
    uint32_t c;MacRuntime r;bar(&r,&c);static uint8_t plain[640*100];memcpy(plain,screen(&r),sizeof plain);
    marker(&r,70,25);
    /* press on the Custom title (cell x 56..120): the title is highlighted, then size, then draw with the rectangle under the title */
    CHECK(msel(&r,5,70)==1 && tracking_idle(&r));
    CHECK(lg(&r,MDEF_SIZE,0,2)==2 && lg(&r,MDEF_DRAW,0,2)==1 && rect_is(&r,MDEF_DRAW,19,64,19+MDEF_H,64+MDEF_W) && lg(&r,MDEF_DRAW,14,4)==c);
    CHECK(px(&r,70,25)==0x55 && px(&r,20,5)==plain[5*640+20] && px(&r,70,5)!=plain[5*640+70]);        /* the definition drew; the title cell is inverted */
    /* the choose message carries the pointer and the item chosen last */
    pointer(&r,100,40);CHECK(again(&r)==1 && tracking_idle(&r) && lg(&r,MDEF_CHOOSE,0,2)==1);
    CHECK(lgs(&r,MDEF_CHOOSE,2)==40 && lgs(&r,MDEF_CHOOSE,4)==100 && lgs(&r,MDEF_CHOOSE,18)==0 && rect_is(&r,MDEF_CHOOSE,19,64,67,144) && lg(&r,MDEF_CHOOSE,14,4)==c);
    pointer(&r,100,66);CHECK(again(&r)==1 && tracking_idle(&r) && lgs(&r,MDEF_CHOOSE,18)==2 && lg(&r,MDEF_CHOOSE,0,2)==2);   /* it answered item 2 before */
    release(&r,100,66);CHECK(again(&r)==1 && !r.menusel && !r.guest.active && mac_cpu_get(r.cpu,MAC_A7)==SP+4 && mac_cpu_get(r.cpu,MAC_PC)==PC);
    CHECK(lgs(&r,MDEF_CHOOSE,18)==3 && rd(&r,SP+4,4)==((140u<<16)|3) && r.menu_hilite==140);        /* the item it answered on the last message */
    { int clean=1;for(int y=19;y<100;y++)for(int x=0;x<640;x++)if(px(&r,x,y)!=plain[y*640+x])clean=0;CHECK(clean);}   /* the pixels under the menu are back */
    CHECK(px(&r,70,5)!=plain[5*640+70]);                                                              /* the title stays highlighted after a choice */
    mac_cpu_destroy(r.cpu);
    /* released outside the rectangle: the definition answers 0 and nothing is chosen */
    bar(&r,&c);memcpy(plain,screen(&r),sizeof plain);marker(&r,70,25);
    CHECK(msel(&r,5,70)==1 && tracking_idle(&r));pointer(&r,300,300);release(&r,300,300);
    CHECK(again(&r)==1 && !r.menusel && rd(&r,SP+4,4)==0 && r.menu_hilite==0 && !memcmp(screen(&r),plain,sizeof plain));
    mac_cpu_destroy(r.cpu);
    /* sliding along the bar: Custom -> File (standard, drawn natively) -> Custom (sized and drawn again) */
    bar(&r,&c);memcpy(plain,screen(&r),sizeof plain);marker(&r,70,25);
    CHECK(msel(&r,5,70)==1 && tracking_idle(&r));
    pointer(&r,20,5);CHECK(again(&r)==1 && r.menusel && !r.guest.active && px(&r,70,25)==0 && px(&r,16,30)==1 && px(&r,103,40)==1 && lg(&r,MDEF_CHOOSE,0,2)==0);
    pointer(&r,100,5);pointer(&r,70,5);CHECK(again(&r)==1 && tracking_idle(&r) && lg(&r,MDEF_SIZE,0,2)==3 && lg(&r,MDEF_DRAW,0,2)==2 && px(&r,70,25)==0x55 && px(&r,16,30)==plain[30*640+16]);
    pointer(&r,100,30);release(&r,100,30);CHECK(again(&r)==1 && !r.menusel && rd(&r,SP+4,4)==((140u<<16)|1));
    mac_cpu_destroy(r.cpu);
    /* sliding from a standard menu onto the custom one, then releasing over a standard item */
    bar(&r,&c);marker(&r,70,25);
    CHECK(msel(&r,5,20)==1 && r.menusel && !r.guest.active);
    pointer(&r,70,5);CHECK(again(&r)==1 && tracking_idle(&r) && lg(&r,MDEF_DRAW,0,2)==1 && px(&r,70,25)==0x55);
    pointer(&r,20,5);CHECK(again(&r)==1 && r.menusel && px(&r,70,25)==0 && px(&r,16,30)==1);
    pointer(&r,30,25);release(&r,30,25);CHECK(again(&r)==1 && !r.menusel && rd(&r,SP+4,4)==((128u<<16)|1));
    mac_cpu_destroy(r.cpu);
}

static void pop_up(void){
    uint32_t c;MacRuntime r;bar(&r,&c);uint32_t p=get_menu(&r,141);insert(&r,p,-1);
    static uint8_t plain[640*480];memcpy(plain,screen(&r),sizeof plain);marker(&r,210,90);
    /* item 2 goes at (top 100, left 200): size, pop-up (the definition places the rectangle), draw, then tracking */
    CHECK(popup(&r,p,100,200,2)==1 && tracking_idle(&r));
    CHECK(lg(&r,MDEF_POPUP,0,2)==1 && lgs(&r,MDEF_POPUP,2)==100 && lgs(&r,MDEF_POPUP,4)==200 && lgs(&r,MDEF_POPUP,18)==2 && lg(&r,MDEF_POPUP,14,4)==p);
    CHECK(lg(&r,MDEF_DRAW,0,2)==1 && rect_is(&r,MDEF_DRAW,84,200,132,280) && px(&r,210,90)==0x55);
    pointer(&r,220,110);CHECK(popup_again(&r)==1 && tracking_idle(&r) && lgs(&r,MDEF_CHOOSE,18)==2 && lgs(&r,MDEF_CHOOSE,2)==110);   /* starts with the pop-up item chosen */
    release(&r,220,120);CHECK(popup_again(&r)==1 && !r.menusel && mac_cpu_get(r.cpu,MAC_A7)==SP+10 && rd(&r,SP+10,4)==((141u<<16)|3) && r.menu_hilite==0);
    CHECK(!memcmp(screen(&r),plain,sizeof plain));
    /* moved to fit the screen: the rectangle the definition chose runs off the right edge */
    marker(&r,600,20);
    CHECK(popup(&r,p,2,630,1)==1 && tracking_idle(&r) && rect_is(&r,MDEF_DRAW,2,560,50,640));
    release(&r,5,300);CHECK(popup_again(&r)==1 && !r.menusel && rd(&r,SP+10,4)==0 && !memcmp(screen(&r),plain,sizeof plain));
    mac_cpu_destroy(r.cpu);
}

static void refusals(void){
    MacRuntime r;rig(&r);
    /* a MENU that names an MDEF the program does not carry halts by name; nothing is allocated for it */
    uint32_t heap=r.heap_next;wr(&r,SP,2,142);CHECK(svc(&r,0xa9bf)==-1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"MDEF 999") && r.heap_next==heap);
    mac_cpu_destroy(r.cpu);
    /* a menuProc that is not a live MDEF handle: the menu is not a valid menu any more */
    for(unsigned k=0;k<3;k++){
        rig(&r);uint32_t c=get_menu(&r,140);MacHandle*m=mac_menu_record(&r,c,NULL);CHECK(m);
        uint32_t bogus[3]={0x1234,c,get_menu(&r,128)};wr(&r,m->pointer+6,4,bogus[k]);
        wr(&r,SP,4,c);CHECK(svc(&r,0xa948)==-1 && mac_cpu_halted(r.cpu));
        mac_cpu_destroy(r.cpu);
    }
    /* the definition's code handle goes stale mid-track: halts, does not guess */
    uint32_t c;bar(&r,&c);marker(&r,70,25);CHECK(msel(&r,5,70)==1 && tracking_idle(&r));
    { MacHandle*m=mac_menu_record(&r,c,NULL);CHECK(m);wr(&r,m->pointer+6,4,0x1234);pointer(&r,100,40);CHECK(again(&r)==-1 && mac_cpu_halted(r.cpu));}
    mac_cpu_destroy(r.cpu);
    /* an MDEF that leaves a negative height */
    rig(&r);c=get_menu(&r,140);{ MacHandle*m=mac_menu_record(&r,c,NULL);CHECK(m);        uint32_t code=mac_mdef_code(&r,rd(&r,m->pointer+6,4));CHECK(code);
        /* patch the MDEF's size answer: MOVE.W #48,4(A0) becomes MOVE.W #-48,4(A0) */
        uint8_t*b=mac_cpu_ram(r.cpu)+code;int patched=0;
        for(unsigned i=0;i+5<400;i+=2)if(b[i]==0x31&&b[i+1]==0x7c&&b[i+2]==0&&b[i+3]==MDEF_H&&b[i+4]==0&&b[i+5]==4){b[i+2]=0xff;b[i+3]=(uint8_t)-MDEF_H;patched=1;}
        CHECK(patched);wr(&r,SP,4,c);CHECK(svc(&r,0xa948)==1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"negative"));   /* the halt comes when the MDEF returns */}
    mac_cpu_destroy(r.cpu);
}
int main(void){
    build_fork();
    sizing();bar_calls();menu_select();pop_up();refusals();
    if(!fails)puts("Custom menu definition checks passed");
    return fails?1:0;
}
