#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_palette_manager_service(MacRuntime*,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x100000;r.heap_limit=0x200000;return r;}
static int create(MacRuntime*r,int n,uint32_t source){mac_cpu_set(r->cpu,MAC_A7,0x8000);wr(r,0x8000,2,0x1234);wr(r,0x8002,2,14);wr(r,0x8004,4,source);wr(r,0x8008,2,(uint16_t)n);return mac_palette_manager_service(r,0xaa91);}
static void successful(void){
 MacRuntime r=fresh();CHECK(create(&r,256,0)==1);uint32_t h=rd(&r,0x800a,4),p=rd(&r,h,4);
 CHECK(h && rd(&r,p,2)==256 && mac_cpu_get(r.cpu,MAC_A7)==0x800a && r.handles[0].size==4112);
 for(unsigned i=0;i<256;i++){CHECK(rd(&r,p+16+i*16,4)==0 && rd(&r,p+20+i*16,2)==0);CHECK(rd(&r,p+22+i*16,2)==14 && rd(&r,p+24+i*16,2)==0x1234);CHECK(rd(&r,p+26+i*16,4)==0);}
 /* Source values aren't palette indices; RGB entries copy in array order. */
 wr(&r,0x5000,4,0x5100);wr(&r,0x5106,2,1);wr(&r,0x5108,2,55);wr(&r,0x510a,4,0x12345678);wr(&r,0x510e,2,0x9abc);
 wr(&r,0x5110,2,23);wr(&r,0x5112,4,0xffff0000);wr(&r,0x5116,2,0x8000);
 r.handles[1]=(MacHandle){.handle=0x5000,.pointer=0x5100,.size=24};r.handle_count=2;
 CHECK(create(&r,3,0x5000)==1);p=rd(&r,rd(&r,0x800a,4),4);CHECK(rd(&r,p+16,4)==0x12345678 && rd(&r,p+20,2)==0x9abc);
 CHECK(rd(&r,p+32,4)==0xffff0000 && rd(&r,p+36,2)==0x8000 && rd(&r,p+48,4)==0);
 CHECK(create(&r,0,0)==1 && rd(&r,rd(&r,rd(&r,0x800a,4),4),2)==0);mac_cpu_destroy(r.cpu);
}
static void failure(void){for(unsigned k=0;k<4;k++){
 MacRuntime r=fresh();uint32_t heap=r.heap_next;
 if(k==3)r.heap_limit=heap+16;
 int result=create(&r,k==0?-1:k==1?257:1,k==2?0x5000:0);
 if(k==3){CHECK(result==1 && rd(&r,0x800a,4)==0 && (int16_t)rd(&r,0x220,2)==-108);}
 else CHECK(result==-1 && mac_cpu_get(r.cpu,MAC_A7)==0x8000);
 CHECK(r.heap_next==heap && r.handle_count==0);mac_cpu_destroy(r.cpu);
}}
static void associations(void){
 MacRuntime r=fresh();r.qd_globals=0x7000;CHECK(mac_color_init(&r)==1);
 mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_toolbox_service(&r,0xa912)==1);
 uint32_t windows[2];
 for(unsigned i=0;i<2;i++){
  uint32_t sp=0x8000;wr(&r,0x4000,4,0x00280032);wr(&r,0x4004,4,0x012c01c2);wr(&r,0x4100,1,0);
  wr(&r,sp,4,0);wr(&r,sp+4,2,0);wr(&r,sp+6,4,0xffffffff);wr(&r,sp+10,2,2);wr(&r,sp+12,2,0);
  wr(&r,sp+14,4,0x4100);wr(&r,sp+18,4,0x4000);wr(&r,sp+22,4,0);mac_cpu_set(r.cpu,MAC_A7,sp);
  CHECK(mac_window_service(&r,0xaa45)==1);windows[i]=rd(&r,sp+26,4);CHECK(windows[i]);
 }
 CHECK(create(&r,16,0)==1);uint32_t h=rd(&r,0x800a,4),port=r.current_port;
 for(unsigned i=0;i<2;i++){
  wr(&r,0x8000,2,i?0x017f:0x007f);wr(&r,0x8002,4,h);wr(&r,0x8006,4,windows[i]);mac_cpu_set(r.cpu,MAC_A7,0x8000);
  CHECK(mac_palette_manager_service(&r,0xaa95)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x800a);
  CHECK(mac_window_find(&r,windows[i])->palette_handle==h && mac_window_find(&r,windows[i])->palette_updates==(i?0xe000:0xc000));
  wr(&r,0x8000,4,windows[i]);mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_palette_manager_service(&r,0xaa96)==1 && rd(&r,0x8004,4)==h);
 }
 CHECK(r.current_port==port);wr(&r,0x8000,4,h);mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_palette_manager_service(&r,0xaa93)==1);
 CHECK(rd(&r,h,4)==0 && !mac_window_find(&r,windows[0])->palette_handle && !mac_window_find(&r,windows[1])->palette_handle);
 CHECK(create(&r,16,0)==1);h=rd(&r,0x800a,4);mac_window_find(&r,windows[0])->palette_handle=h;
 wr(&r,0x8000,4,windows[0]);mac_cpu_set(r.cpu,MAC_A7,0x8000);
 /* NewPalette stored usage 14 (explicit, animated, tolerant), which activation now accepts with the explicit bit winning
  * (test_mac_palette_activation). Make the entries plain animated (usage 4), which activation does not support: showing the
  * window must refuse BEFORE the window appears, with the device table untouched. */
 {uint32_t pal=rd(&r,h,4);for(unsigned i=0;i<16;i++)wr(&r,pal+16+i*16+6,2,4);}
 {uint32_t table[256];memcpy(table,r.palette,sizeof table);
  CHECK(mac_window_service(&r,0xa915)==-1 && mac_cpu_halted(r.cpu) && !mac_window_find(&r,windows[0])->visible && !memcmp(table,r.palette,sizeof table));}
 mac_cpu_destroy(r.cpu);
}
static void dispatched(void){
 MacRuntime r=fresh();mac_traps_init(&r);mac_cpu_set(r.cpu,MAC_A7,0x8000);wr(&r,0x8008,2,16);
 CHECK(mac_runtime_trap(r.cpu,0xaa91,0x4000,&r)==0);
 CHECK(!mac_cpu_halted(r.cpu) && rd(&r,0x800a,4) && mac_cpu_get(r.cpu,MAC_A7)==0x800a);
 mac_cpu_destroy(r.cpu);
}
/* _PaletteDispatch: HasDepth ($0A14) and SetDepth ($0A13). Arguments are pushed in Pascal order, so from the
 * top of stack: flags, whichFlags, depth, gd (10 bytes), then the 2-byte function result above them. */
static uint16_t depth_call(MacRuntime*r,uint16_t selector,uint32_t gd,int depth,unsigned which,unsigned flags){
 mac_cpu_set(r->cpu,MAC_A7,0x8000);mac_cpu_set(r->cpu,MAC_D0,selector);
 wr(r,0x8000,2,flags);wr(r,0x8002,2,which);wr(r,0x8004,2,(uint16_t)depth);wr(r,0x8006,4,gd);wr(r,0x800a,2,0xbeef);
 CHECK(mac_palette_manager_service(r,0xaaa2)==1);
 CHECK(mac_cpu_get(r->cpu,MAC_A7)==0x800a);
 return (uint16_t)rd(r,0x800a,2);
}
static void palette_dispatch(void){
 MacRuntime r=fresh();CHECK(mac_color_init(&r)==1);uint32_t gd=r.main_device;CHECK(gd && r.screen_depth==8);
 /* HasDepth: the single 8-bit indexed mode, available as color or as grays. */
 CHECK(depth_call(&r,0x0a14,gd,8,1,1)==1);          /* 8-bit color: available */
 CHECK(depth_call(&r,0x0a14,gd,8,1,0)==1);          /* 8-bit grays: available (Glider PRO's CanWeDisplay8Bit) */
 CHECK(depth_call(&r,0x0a14,gd,8,0,0)==1);          /* device type not constrained */
 CHECK(depth_call(&r,0x0a14,gd,1,1,0)==0 && depth_call(&r,0x0a14,gd,4,1,0)==0);   /* no other depth exists */
 CHECK(depth_call(&r,0x0a14,gd,4,1,1)==0 && depth_call(&r,0x0a14,gd,32,1,1)==0 && depth_call(&r,0x0a14,gd,-8,1,1)==0);
 CHECK(depth_call(&r,0x0a14,gd,8,2,2)==0);          /* undefined flag bits are not claimed */
 CHECK(depth_call(&r,0x0a14,0,8,1,1)==0 && depth_call(&r,0x0a14,gd+4,8,1,1)==0);   /* not the main device */
 /* SetDepth switches between the color and gray tables for real; other depths fail and change nothing. */
 uint32_t ct=rd(&r,r.screen_colors,4),devp=rd(&r,gd,4);
 uint8_t color[2056];memcpy(color,mac_cpu_ram(r.cpu)+ct,2056);
 uint32_t seed=rd(&r,ct,4);
 CHECK(rd(&r,devp+20,2)&1);
 CHECK(depth_call(&r,0x0a13,gd,8,1,0)==0);                              /* to grays */
 CHECK(!(rd(&r,devp+20,2)&1) && r.screen_depth==8 && rd(&r,ct,4)==seed+1);
 for(unsigned i=0;i<256;i++){
  uint32_t rr=rd(&r,ct+10+i*8,2),gg=rd(&r,ct+12+i*8,2),bb=rd(&r,ct+14+i*8,2);
  CHECK(rr==gg && gg==bb && rr==(255-i)*257);                            /* white at 0 ... black at 255 */
  CHECK(r.palette[i]==((255-i)<<16|(255-i)<<8|(255-i)));
 }
 CHECK(depth_call(&r,0x0a14,gd,8,1,0)==1 && depth_call(&r,0x0a14,gd,8,1,1)==1);   /* both still offered */
 CHECK(depth_call(&r,0x0a13,gd,8,0,0)==0 && !(rd(&r,devp+20,2)&1));    /* whichFlags 0: leave the type alone */
 CHECK(depth_call(&r,0x0a13,gd,8,1,1)==0);                              /* back to color */
 CHECK((rd(&r,devp+20,2)&1) && rd(&r,ct,4)==seed+2);
 CHECK(memcmp(mac_cpu_ram(r.cpu)+ct+4,color+4,2052)==0);                /* identical to the original table */
 CHECK(depth_call(&r,0x0a13,gd,1,1,0)==(uint16_t)-50 && r.screen_depth==8 && (rd(&r,devp+20,2)&1));
 CHECK(depth_call(&r,0x0a13,gd,8,2,0)==(uint16_t)-50 && rd(&r,ct,4)==seed+2);       /* undefined flag bits */
 CHECK(depth_call(&r,0x0a13,0,8,1,1)==(uint16_t)-50 && rd(&r,ct,4)==seed+2);
 CHECK(!mac_cpu_halted(r.cpu));
 /* Unknown routines and malformed argument sizes halt naming the selector; nothing is fabricated. */
 mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0415);
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu));
 CHECK(strstr(mac_cpu_error(r.cpu),"0x0415")!=NULL);
 mac_cpu_destroy(r.cpu);
 r=fresh();mac_color_init(&r);mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0814);   /* HasDepth, wrong size */
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu));
 mac_cpu_destroy(r.cpu);
 r=fresh();mac_color_init(&r);mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-4);mac_cpu_set(r.cpu,MAC_D0,0x0a14);  /* stack runs off RAM */
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu));
 mac_cpu_destroy(r.cpu);
}
/* AnimatePalette ($AA9A). A visible window whose 8-entry palette is explicit and animated (usage 14, as Castle of Dr. Brain's is),
 * activated; a 4-entry source color table at handle 0x5200. */
#define ANIM_CT 0x5200u
static uint32_t dev(unsigned r,unsigned g,unsigned b){return (uint32_t)(r>>8)<<16|(uint32_t)(g>>8)<<8|(b>>8);}
static MacRuntime animate_rig(uint32_t*window,uint32_t*palette){
 MacRuntime r=fresh();r.qd_globals=0x7000;CHECK(mac_color_init(&r)==1);
 mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_toolbox_service(&r,0xa912)==1);
 uint32_t sp=0x8000;wr(&r,0x4000,4,0x00280032);wr(&r,0x4004,4,0x012c01c2);wr(&r,0x4100,1,0);
 wr(&r,sp,4,0);wr(&r,sp+4,2,0);wr(&r,sp+6,4,0xffffffff);wr(&r,sp+10,2,2);wr(&r,sp+12,2,0x0100);
 wr(&r,sp+14,4,0x4100);wr(&r,sp+18,4,0x4000);wr(&r,sp+22,4,0);mac_cpu_set(r.cpu,MAC_A7,sp);
 CHECK(mac_window_service(&r,0xaa45)==1);*window=rd(&r,sp+26,4);
 CHECK(create(&r,8,0)==1);*palette=rd(&r,0x800a,4);
 MacWindowState*w=mac_window_find(&r,*window);CHECK(w && w->visible);w->palette_handle=*palette;
 CHECK(mac_palette_activate(&r,w)==0 && r.palette_active==*palette);
 wr(&r,ANIM_CT,4,ANIM_CT+0x100);wr(&r,ANIM_CT+0x100+6,2,3);          /* four entries */
 for(unsigned i=0;i<4;i++){uint32_t e=ANIM_CT+0x100+8+i*8;wr(&r,e,2,i);wr(&r,e+2,2,0x1100*(i+1));wr(&r,e+4,2,0x2200*(i+1));wr(&r,e+6,2,0x3300*(i+1)&0xffff);}
 r.handles[r.handle_count++]=(MacHandle){.handle=ANIM_CT,.pointer=ANIM_CT+0x100,.size=8+4*8};
 return r;
}
static int animate_call(MacRuntime*r,uint32_t window,uint32_t table,int index,int entry,int length){
 mac_cpu_set(r->cpu,MAC_A7,0x8000);wr(r,0x8000,2,(uint16_t)length);wr(r,0x8002,2,(uint16_t)entry);wr(r,0x8004,2,(uint16_t)index);wr(r,0x8006,4,table);wr(r,0x800a,4,window);
 return mac_palette_manager_service(r,0xaa9a);
}
static void animate(void){
 uint32_t window,palette;MacRuntime r=animate_rig(&window,&palette);uint32_t saved[256];memcpy(saved,r.palette,sizeof saved);
 uint32_t stored[8];for(unsigned i=0;i<8;i++)stored[i]=rd(&r,rd(&r,palette,4)+16+i*16,4);
 /* Two entries from source entry 1: device entries 3 and 4 take the colors; nothing else on the device changes. */
 CHECK(animate_call(&r,window,ANIM_CT,1,3,2)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x800e && !mac_cpu_halted(r.cpu));
 CHECK(r.palette[3]==dev(0x2200,0x4400,0x6600) && r.palette[4]==dev(0x3300,0x6600,0x9900));
 for(unsigned i=0;i<256;i++)if(i!=3 && i!=4)CHECK(r.palette[i]==saved[i]);
 {uint32_t ct=rd(&r,r.screen_colors,4);CHECK(rd(&r,ct+10+3*8,2)==0x2200 && rd(&r,ct+12+3*8,2)==0x4400 && rd(&r,ct+14+3*8,2)==0x6600);}   /* the device color table itself */
 for(unsigned i=0;i<8;i++)CHECK(rd(&r,rd(&r,palette,4)+16+i*16,4)==stored[i]);          /* the palette's own colors are not rewritten */
 /* The device index is the one activation chose. */
 r.palette_map[5]=40;CHECK(animate_call(&r,window,ANIM_CT,0,5,1)==1 && r.palette[40]==dev(0x1100,0x2200,0x3300) && r.palette[5]==saved[5]);
 /* A zero length does nothing and pops; the whole table and the whole palette are reachable. */
 memcpy(saved,r.palette,sizeof saved);CHECK(animate_call(&r,window,ANIM_CT,0,0,0)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x800e && !memcmp(saved,r.palette,sizeof saved));
 CHECK(animate_call(&r,window,ANIM_CT,0,4,4)==1 && r.palette[7]==dev(0x4400,0x8800,0xcc00));
 CHECK(animate_call(&r,window,ANIM_CT,3,7,1)==1 && r.palette[7]==dev(0x4400,0x8800,0xcc00));
 mac_cpu_destroy(r.cpu);

 /* A window without a palette has no animated entries: the call changes nothing and pops its arguments. A NIL window means the
  * frontmost window; with no window at all it halts. */
 {MacRuntime q=animate_rig(&window,&palette);uint32_t before[256];memcpy(before,q.palette,sizeof before);
  mac_window_find(&q,window)->palette_handle=0;
  CHECK(animate_call(&q,window,ANIM_CT,0,2,3)==1 && mac_cpu_get(q.cpu,MAC_A7)==0x800e && !mac_cpu_halted(q.cpu) && !memcmp(before,q.palette,sizeof before));
  mac_window_find(&q,window)->palette_handle=palette;
  CHECK(animate_call(&q,0,ANIM_CT,0,2,2)==1 && q.palette[2]==dev(0x1100,0x2200,0x3300) && q.palette[3]==dev(0x2200,0x4400,0x6600));   /* NIL = the frontmost window */
  mac_cpu_destroy(q.cpu);}
 {MacRuntime q=animate_rig(&window,&palette);              /* NIL with no window at all */
  mac_cpu_set(q.cpu,MAC_A7,0x8000);wr(&q,0x8000,4,window);CHECK(mac_window_service(&q,0xa92d)==1 && !mac_window_front(&q));
  CHECK(animate_call(&q,0,ANIM_CT,0,2,2)==-1 && mac_cpu_halted(q.cpu));
  mac_cpu_destroy(q.cpu);}
 /* Refusals halt and change nothing, including the entries that came before the bad one. */
 for(unsigned k=0;k<12;k++){
  if(k==7)continue;                                                   /* (a window without a palette is a no-op, below) */
  MacRuntime q=animate_rig(&window,&palette);uint32_t before[256];memcpy(before,q.palette,sizeof before);uint32_t table=ANIM_CT;int idx=0,ent=2,len=3;uint32_t win=window;
  if(k==0)wr(&q,rd(&q,palette,4)+16+4*16+6,2,10);                    /* entry 4 is not animated (usage 10) */
  if(k==1)q.palette_active=0;                                         /* the palette is not the active one */
  if(k==2)win=0x1234;                                                 /* not a window */
  if(k==3)len=-1;
  if(k==4){ent=6;len=3;wr(&q,rd(&q,palette,4)+16+8*16+6,2,14);}      /* past the palette (6+3 > 8), where the next bytes look animated */
  if(k==5)table=0x6000;                                               /* not an owned handle */
  if(k==6){idx=2;len=3;}                                              /* past the color table (2+3 > 4) */
  if(k==8)ent=-1;
  if(k==9)idx=-1;
  if(k==10)q.handles[q.handle_count-1].size=8+2*8;                    /* a truncated color table */
  int rc;
  if(k==11){mac_cpu_set(q.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(q.cpu)-6);rc=mac_palette_manager_service(&q,0xaa9a);}
  else rc=animate_call(&q,win,table,idx,ent,len);
  CHECK(rc==-1 && mac_cpu_halted(q.cpu) && !memcmp(before,q.palette,sizeof before));
  if(k==4)CHECK(strstr(mac_cpu_error(q.cpu),"exceed")!=NULL);
  if(k==11)CHECK(strstr(mac_cpu_error(q.cpu),"stack")!=NULL);
  mac_cpu_destroy(q.cpu);
 }
}
static uint32_t rdw_raw(MacRuntime*r,uint32_t p){const uint8_t*m=mac_cpu_ram(r->cpu);return (uint32_t)m[p]<<24|m[p+1]<<16|m[p+2]<<8|m[p+3];}

/* PmForeColor ($AA97) / PmBackColor ($AA98): the current port takes the device index and RGB of a palette entry. */
static int pm_call(MacRuntime*r,uint16_t op,int entry){mac_cpu_set(r->cpu,MAC_A7,0x8000);wr(r,0x8000,2,(uint16_t)entry);return mac_palette_manager_service(r,op);}
static void pm_colors(void){
    uint32_t window,palette;MacRuntime r=animate_rig(&window,&palette);uint32_t port=r.current_port;
    CHECK(port==window);
    uint32_t pal=rd(&r,palette,4);
    wr(&r,pal+16+16*3,2,0x1111);wr(&r,pal+16+16*3+2,2,0x2222);wr(&r,pal+16+16*3+4,2,0x3333);
    wr(&r,pal+16+16*5,2,0x4444);wr(&r,pal+16+16*5+2,2,0x5555);wr(&r,pal+16+16*5+4,2,0x6666);
    r.palette_map[3]=40;r.palette_map[5]=77;
    wr(&r,port+80,4,200);wr(&r,port+84,4,7);
    CHECK(pm_call(&r,0xaa97,3)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x8002);
    CHECK(rd(&r,port+80,4)==40 && rd(&r,port+36,2)==0x1111 && rd(&r,port+38,2)==0x2222 && rd(&r,port+40,2)==0x3333 && rd(&r,port+84,4)==7);   /* the foreground only */
    CHECK(pm_call(&r,0xaa98,5)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x8002);
    CHECK(rd(&r,port+84,4)==77 && rd(&r,port+42,2)==0x4444 && rd(&r,port+44,2)==0x5555 && rd(&r,port+46,2)==0x6666 && rd(&r,port+80,4)==40);   /* the background only */
    CHECK(pm_call(&r,0xaa97,0)==1 && rd(&r,port+80,4)==r.palette_map[0]);                                                                  /* the first entry */
    CHECK(pm_call(&r,0xaa97,7)==1);                                                                                                         /* the last of eight */
    CHECK(pm_call(&r,0xaa99,0)==0 && pm_call(&r,0xaa9b,0)==0);                                                                              /* neighbours are not ours */
    mac_cpu_destroy(r.cpu);
    for(unsigned k=0;k<7;k++){
        r=animate_rig(&window,&palette);uint32_t before=rd(&r,r.current_port+80,4);int entry=2;uint16_t op=0xaa97;
        if(k==0)entry=8;                                              /* one past the palette */
        if(k==1)entry=-1;
        if(k==2)r.current_port=0;                                     /* no port */
        if(k==3)mac_window_find(&r,window)->palette_handle=0;         /* the window has no palette */
        if(k==4)r.palette_active=0;                                   /* its palette is not the active one */
        if(k==5)r.current_port=0x1234;                                /* not a window's port */
        if(k==6){op=0xaa98;entry=99;}
        int rc=pm_call(&r,op,entry);
        CHECK(rc==-1 && mac_cpu_halted(r.cpu));
        if(k<2||k==6){CHECK(rdw_raw(&r,window+80)==before);}
        mac_cpu_destroy(r.cpu);
    }
    r=animate_rig(&window,&palette);mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-1);CHECK(mac_palette_manager_service(&r,0xaa97)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
}
static void restore_clut(void){
 MacRuntime r=fresh();CHECK(mac_color_init(&r)==1);uint32_t gd=r.main_device;
 uint32_t white=r.palette[0],black=r.palette[255],e5=r.palette[5];
 for(unsigned i=0;i<256;i++)r.palette[i]=0x123456;r.palette_active=0x9999;
 mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0002);wr(&r,0x8000,4,gd);
 CHECK(mac_palette_manager_service(&r,0xaaa2)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x8004 && !mac_cpu_halted(r.cpu));      /* RestoreDeviceClut(gd): table back to its default */
 CHECK(r.palette[0]==white && r.palette[255]==black && r.palette[5]==e5 && r.palette_active==0);
 for(unsigned i=0;i<256;i++)r.palette[i]=0x654321;
 mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0002);wr(&r,0x8000,4,0);                                   /* nil: every device */
 CHECK(mac_palette_manager_service(&r,0xaaa2)==1 && r.palette[0]==white && mac_cpu_get(r.cpu,MAC_A7)==0x8004);
 mac_cpu_destroy(r.cpu);
 r=fresh();CHECK(mac_color_init(&r)==1);r.palette[0]=0x111111;                                                          /* a device we do not have halts, changing nothing */
 mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0002);wr(&r,0x8000,4,0x7000);
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu) && r.palette[0]==0x111111);mac_cpu_destroy(r.cpu);
 r=fresh();CHECK(mac_color_init(&r)==1);                                                                                /* a stack at the end of RAM halts */
 mac_cpu_set(r.cpu,MAC_A7,(uint32_t)mac_cpu_ram_size(r.cpu)-2);mac_cpu_set(r.cpu,MAC_D0,0x0002);
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 r=fresh();CHECK(mac_color_init(&r)==1);mac_cpu_set(r.cpu,MAC_A7,0x8000);mac_cpu_set(r.cpu,MAC_D0,0x0012);              /* a neighbouring selector is still refused */
 CHECK(mac_palette_manager_service(&r,0xaaa2)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
}
int main(void){restore_clut();animate();pm_colors();palette_dispatch();dispatched();successful();failure();associations();puts("mac palette manager object and association tests passed");return 0;}
