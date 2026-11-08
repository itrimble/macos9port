/* MenuSelect, HiliteMenu and MenuKey on a real menu bar built through NewMenu, AppendMenu and InsertMenu. */
#include "mac_menu_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fails++;}}while(0)
static int fails;
#define SP 0x8000u
#define PC 0x5002u
#define SCR 0xe00000u
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void str(MacRuntime*r,uint32_t at,const char*s){size_t n=strlen(s);mac_cpu_ram(r->cpu)[at]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+at+1,s,n);}
static int mcall(MacRuntime*r,unsigned op){mac_cpu_set(r->cpu,MAC_A7,SP);return mac_menu_service(r,(uint16_t)op);}
static uint32_t new_menu(MacRuntime*r,unsigned id,const char*title,const char*items){
    str(r,0x4000,title);wr(r,SP,4,0x4000);wr(r,SP+4,2,id);CHECK(mcall(r,0xa931)==1);uint32_t h=rd(r,SP+6,4);
    str(r,0x4100,items);wr(r,SP,4,0x4100);wr(r,SP+4,4,h);CHECK(mcall(r,0xa933)==1);          /* AppendMenu(h, items): a pointer to the Pascal string */
    wr(r,SP,2,0);wr(r,SP+2,4,h);CHECK(mcall(r,0xa935)==1);return h;                        /* InsertMenu(h, 0) */
}
static MacRuntime rig(uint32_t*file){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x10000;r.heap_limit=0x20000;
    r.screen_base=SCR;r.screen_width=640;r.screen_height=480;r.screen_row_bytes=640;r.screen_depth=8;r.palette[0]=0xffffff;
    CHECK(mac_toolbox_service(&r,0xa930)==1);wr(&r,0xbaa,2,20);
    str(&r,0x4300,"x");
    uint32_t f=new_menu(&r,128,"File","Open/O;-;(Close;Quit/Q");
    uint32_t edit=new_menu(&r,129,"Edit","Cut/X;Copy/C");
    new_menu(&r,130,"Options","Alpha/a;Beta");
    *file=f;
    { MacHandle*m=mac_menu_record(&r,edit,NULL);CHECK(m);wr(&r,m->pointer+10,4,0xfffffffe);}               /* the whole Edit menu disabled */
    CHECK(mcall(&r,0xa937)==1);memset(mac_cpu_ram(r.cpu)+0x6000,0,16);
    mac_cpu_set(r.cpu,MAC_A7,SP);return r;
}
static uint8_t *screen(MacRuntime*r){return mac_cpu_ram(r->cpu)+SCR;}
static unsigned px(MacRuntime*r,int x,int y){return screen(r)[y*640+x];}
/* One execution of the MenuSelect trap: PC is just past the trap word, as after a fetch. */
static int msel(MacRuntime*r,int v,int h){wr(r,SP,2,(uint16_t)v);wr(r,SP+2,2,(uint16_t)h);wr(r,SP+4,4,0xdeadbeef);mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);return mac_menuselect_service(r,0xa93d);}
static int again(MacRuntime*r){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_PC,PC);return mac_menuselect_service(r,0xa93d);}
static void pointer(MacRuntime*r,int x,int y){r->mouse_x=(int16_t)x;r->mouse_y=(int16_t)y;}
static void release(MacRuntime*r,int x,int y){mac_runtime_post_event(r,2,0,0x80,(int16_t)x,(int16_t)y);}
static uint32_t result(MacRuntime*r){return rd(r,SP+4,4);}
static int unchanged(MacRuntime*r,const uint8_t*base){return !memcmp(screen(r),base,640*100);}

static void tracking(void){
    uint32_t f;MacRuntime r=rig(&f);static uint8_t bar[640*100];memcpy(bar,screen(&r),sizeof bar);
    /* press on File (cell x 8..55): the title is inverted, a pull-down is drawn below the bar, the trap rewinds */
    CHECK(msel(&r,5,30)==1 && r.menusel && mac_cpu_get(r.cpu,MAC_PC)==0x5000 && mac_cpu_get(r.cpu,MAC_A7)==SP);
    CHECK(px(&r,12,5)!=bar[5*640+12] || px(&r,20,5)!=bar[5*640+20]);                    /* the title cell differs from the plain bar */
    CHECK(px(&r,16,30)==1 && px(&r,16,19)==1 && px(&r,103,30)==1);                       /* the pull-down's dark frame */
    CHECK(px(&r,98,22)==0);                                                              /* an item row, not highlighted */
    pointer(&r,40,25);CHECK(again(&r)==1 && r.menusel && px(&r,98,22)==1);              /* over Open: its row is inverted */
    pointer(&r,40,38);CHECK(again(&r)==1 && px(&r,98,22)==0 && px(&r,98,36)==0);        /* over the separator: nothing is highlighted */
    pointer(&r,40,50);CHECK(again(&r)==1 && px(&r,98,46)==0);                            /* over the disabled Close: nothing */
    pointer(&r,40,62);CHECK(again(&r)==1 && px(&r,98,58)==1);                            /* over Quit */
    release(&r,40,62);CHECK(again(&r)==1 && !r.menusel && mac_cpu_get(r.cpu,MAC_A7)==SP+4 && result(&r)==((128u<<16)|4) && r.menu_hilite==128);
    {int clean=1;for(int y=19;y<100;y++)for(int x=0;x<640;x++)if(px(&r,x,y)!=bar[y*640+x])clean=0;CHECK(clean);}   /* the pull-down is gone, the pixels restored */
    CHECK(px(&r,20,5)!=bar[5*640+20]);                                                   /* the title stays highlighted after a choice */
    wr(&r,SP,2,0);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa938)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2 && r.menu_hilite==0 && unchanged(&r,bar));   /* HiliteMenu(0) */
    mac_cpu_destroy(r.cpu);
    /* releasing over a disabled item, a separator, or outside the menu gives 0 and un-highlights the title */
    static const int spots[][2]={{40,50},{40,38},{300,300},{40,10},{40,69},{40,70}};     /* ... and just below the last item (the pull-down ends at y 69) */
    for(unsigned k=0;k<6;k++){
        r=rig(&f);memcpy(bar,screen(&r),sizeof bar);CHECK(msel(&r,5,30)==1);pointer(&r,spots[k][0],spots[k][1]);release(&r,spots[k][0],spots[k][1]);
        CHECK(again(&r)==1 && !r.menusel && mac_cpu_get(r.cpu,MAC_A7)==SP+4 && result(&r)==0 && r.menu_hilite==0 && unchanged(&r,bar));
        mac_cpu_destroy(r.cpu);
    }
    /* sliding along the bar switches menus: Options (cell x 104..175) is third; release on its second item */
    r=rig(&f);memcpy(bar,screen(&r),sizeof bar);CHECK(msel(&r,5,30)==1);
    pointer(&r,120,5);CHECK(again(&r)==1 && r.menusel && r.menusel);                      /* Options' pull-down replaces File's */
    CHECK(px(&r,112,30)==1 && px(&r,30,30)==bar[30*640+30]);                                /* its frame is drawn; File's area is back to the plain screen */
    pointer(&r,120,37);release(&r,120,37);CHECK(again(&r)==1 && !r.menusel && result(&r)==((130u<<16)|2) && r.menu_hilite==130);
    mac_cpu_destroy(r.cpu);
    /* sliding onto the disabled Edit title keeps File's menu; releasing on the bar over Edit gives 0 */
    r=rig(&f);CHECK(msel(&r,5,30)==1);pointer(&r,80,5);CHECK(again(&r)==1 && r.menusel && px(&r,16,30)==1 && px(&r,103,40)==1);release(&r,80,5);CHECK(again(&r)==1 && result(&r)==0);mac_cpu_destroy(r.cpu);
    /* a press that is not on an enabled title returns 0 at once and leaves no state */
    for(unsigned k=0;k<5;k++){
        r=rig(&f);memcpy(bar,screen(&r),sizeof bar);int v=5,h=300;if(k==1){h=80;}if(k==2){v=40;h=30;}if(k==3){h=4;}if(k==4){v=-3;h=20;}
        CHECK(msel(&r,v,h)==1 && !r.menusel && mac_cpu_get(r.cpu,MAC_A7)==SP+4 && result(&r)==0 && unchanged(&r,bar) && r.menu_hilite==0);mac_cpu_destroy(r.cpu);
    }
    /* a stale highlight from an earlier choice is cleared by the next MenuSelect */
    r=rig(&f);memcpy(bar,screen(&r),sizeof bar);msel(&r,5,30);pointer(&r,40,25);again(&r);release(&r,40,25);again(&r);CHECK(r.menu_hilite==128);
    CHECK(msel(&r,5,300)==1 && r.menu_hilite==0 && unchanged(&r,bar));mac_cpu_destroy(r.cpu);
}
static void keys_and_hilite(void){
    uint32_t f;MacRuntime r=rig(&f);static uint8_t bar[640*100];memcpy(bar,screen(&r),sizeof bar);
    #define KEY(c) (wr(&r,SP,2,(unsigned)(c)<<8),wr(&r,SP+2,4,0xdeadbeef),mac_cpu_set(r.cpu,MAC_A7,SP),mac_menuselect_service(&r,0xa93e))
    CHECK(KEY('o')==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+2 && rd(&r,SP+2,4)==((128u<<16)|1) && r.menu_hilite==128 && px(&r,20,5)!=bar[5*640+20]);   /* case-insensitive; highlights File */
    CHECK(KEY('Q')==1 && rd(&r,SP+2,4)==((128u<<16)|4) && r.menu_hilite==128);                                                                  /* still one highlighted title */
    CHECK(KEY('C')==1 && rd(&r,SP+2,4)==0);                                                                                                       /* Edit is disabled, and Close has no key */
    CHECK(KEY('A')==1 && rd(&r,SP+2,4)==((130u<<16)|1) && r.menu_hilite==130);                                                                   /* a lowercase key in the menu matches an uppercase press */
    CHECK(KEY('Z')==1 && rd(&r,SP+2,4)==0 && KEY(0)==1 && rd(&r,SP+2,4)==0);
    wr(&r,SP,2,0);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa938)==1 && r.menu_hilite==0 && unchanged(&r,bar));
    /* HiliteMenu: highlight one, then another (the first is cleared), then an unknown ID (clears) */
    wr(&r,SP,2,130);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa938)==1 && r.menu_hilite==130 && px(&r,120,5)!=bar[5*640+120]);
    wr(&r,SP,2,128);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa938)==1 && r.menu_hilite==128 && px(&r,120,5)==bar[5*640+120] && px(&r,20,5)!=bar[5*640+20]);
    wr(&r,SP,2,999);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa938)==1 && r.menu_hilite==0 && unchanged(&r,bar));
    /* DrawMenuBar repaints every title, so it clears the highlight record */
    wr(&r,SP,2,128);mac_cpu_set(r.cpu,MAC_A7,SP);mac_menuselect_service(&r,0xa938);CHECK(mcall(&r,0xa937)==1 && r.menu_hilite==0 && unchanged(&r,bar));
    CHECK(mac_menuselect_service(&r,0xa93f)==0 && mac_menuselect_service(&r,0xa93c)==0);   /* neighbours are not ours */
    mac_cpu_destroy(r.cpu);
}
static void refusals(void){
    uint32_t f;MacRuntime r;
    for(unsigned k=0;k<8;k++){
        r=rig(&f);uint32_t sp=SP;
        if(k==0)sp=SP+1;                                                  /* odd stack */
        if(k==1)sp=(uint32_t)mac_cpu_ram_size(r.cpu)-4;                   /* no room for the result */
        if(k==2)r.screen_depth=1;                                         /* not the 8-bit screen */
        if(k==3)r.manager_flags&=~4u;                                     /* no Menu Manager state */
        if(k==4){msel(&r,5,30);wr(&r,SP,2,0);mac_cpu_set(r.cpu,MAC_A7,SP-2);mac_cpu_set(r.cpu,MAC_PC,PC);}   /* called again from a different place while tracking */
        if(k==5)r.screen_row_bytes=100;                                   /* a row narrower than the screen */
        if(k==6){msel(&r,5,30);wr(&r,0xa1c,4,0);}                         /* the menu list vanishes mid-track */
        if(k==7)mac_cpu_halt(r.cpu,"stopped");
        int rc;
        if(k==4)rc=mac_menuselect_service(&r,0xa93d);
        else if(k==6){pointer(&r,40,25);rc=again(&r);}
        else {mac_cpu_set(r.cpu,MAC_A7,sp);mac_cpu_set(r.cpu,MAC_PC,PC);rc=mac_menuselect_service(&r,0xa93d);}
        CHECK(rc==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    }
    /* HiliteMenu and MenuKey: bad stack, tracking in progress */
    r=rig(&f);mac_cpu_set(r.cpu,MAC_A7,SP+1);CHECK(mac_menuselect_service(&r,0xa938)==-1);mac_cpu_destroy(r.cpu);
    r=rig(&f);msel(&r,5,30);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_menuselect_service(&r,0xa93e)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    /* A corrupt menu record means that menu is simply not there (nothing is read out of range): no tracking */
    r=rig(&f);{MacHandle*m=mac_menu_record(&r,rd(&r,rd(&r,r.menu_bar,4)+2+8,4),NULL);CHECK(m);mac_cpu_ram(r.cpu)[m->pointer+15+7]=250;}      /* the first item's length byte of the third menu (Options, title length 7) */
    CHECK(msel(&r,5,30)==1 && !r.menusel && result(&r)==0 && !mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
}
int main(void){
    tracking();keys_and_hilite();refusals();
    if(!fails)puts("Menu tracking checks passed");
    return fails?1:0;
}
