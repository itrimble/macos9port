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
static uint32_t region_args(MacRuntime *r,int t,int l,int b,int right){
 mac_cpu_set(r->cpu,MAC_A7,SP);CHECK(mac_regions_service(r,0xa8d8)==1);uint32_t h=rd(r,SP,4),p=rd(r,h,4);
 wr(r,p+2,2,(uint16_t)t);wr(r,p+4,2,(uint16_t)l);wr(r,p+6,2,(uint16_t)b);wr(r,p+8,2,(uint16_t)right);return h;
}
static int paint(MacRuntime*r,uint32_t h){mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,4,h);wr(r,SP+4,4,STORAGE);return mac_window_service(r,0xa90d);}
static void repaint(void){
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 CHECK(call(&r,0xa922,STORAGE)==1 && call(&r,0xa923,STORAGE)==1);
 uint32_t up=rd(&r,r.window.handles[10],4),h=region_args(&r,20,49,80,100);
 uint8_t expected[307200];memcpy(expected,mac_cpu_ram(r.cpu)+r.screen_base,sizeof expected);
 memset(mac_cpu_ram(r.cpu)+r.screen_base,77,sizeof expected);wr(&r,0x9da,2,1);wr(&r,0x9dc,2,0);
 CHECK(paint(&r,h)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+8 && rd(&r,0x9dc,2)==1);
 CHECK(rd(&r,up+2,2)==40 && rd(&r,up+4,2)==50 && rd(&r,up+6,2)==80 && rd(&r,up+8,2)==100);
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++){
  int frame=y>=20 && y<80 && x>=49 && x<100 && (y<40 || x<50);
  CHECK(mac_cpu_ram(r.cpu)[r.screen_base+y*640+x]==(frame?expected[y*640+x]:77));
 }
 MacEvent event;CHECK(mac_window_update_event(&r,1<<6,&event)==1 && event.message==STORAGE);
 CHECK(call(&r,0xa922,STORAGE)==1 && call(&r,0xa923,STORAGE)==1);wr(&r,0x9da,2,0);
 CHECK(paint(&r,h)==1 && rd(&r,up+2,4)==0 && rd(&r,up+6,4)==0);
 CHECK(!mac_window_update_event(&r,1<<6,&event));mac_cpu_destroy(r.cpu);
}
/* A complex clobbered region: the part inside the window is treated as its bounding box; reaching the screen outside the window is refused; the part off the screen is ignored. */
static uint32_t union2(MacRuntime *r,uint32_t a,uint32_t b){
 mac_cpu_set(r->cpu,MAC_A7,SP);CHECK(mac_regions_service(r,0xa8d8)==1);uint32_t c=rd(r,SP,4);
 mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,4,c);wr(r,SP+4,4,b);wr(r,SP+8,4,a);CHECK(mac_regions_service(r,0xa8e5)==1);return c;
}
static void complex_region(void){
 for(unsigned k=0;k<5;k++){
  MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
  CHECK(call(&r,0xa922,STORAGE)==1 && call(&r,0xa923,STORAGE)==1);
  uint32_t up=rd(&r,r.window.handles[10],4);
  uint32_t a=k==4?region_args(&r,100,639,110,640):k==2?region_args(&r,-50,-50,-10,-10):k==3?region_args(&r,60,60,80,100):region_args(&r,50,60,60,100);
  uint32_t b=k==4?region_args(&r,200,639,210,640):k==2?region_args(&r,-5,-60,-1,-20):k==3?region_args(&r,400,0,420,10):region_args(&r,60,60,80,70);
  uint32_t c=union2(&r,a,b);CHECK(rd(&r,rd(&r,c,4),2)>10);                                       /* really complex */
  wr(&r,0x9da,2,1);uint8_t before[307200];memcpy(before,mac_cpu_ram(r.cpu)+r.screen_base,sizeof before);
  int rc=paint(&r,c);
  if(k==3||k==4){CHECK(rc==-1&&mac_cpu_halted(r.cpu)&&!memcmp(before,mac_cpu_ram(r.cpu)+r.screen_base,sizeof before));}   /* it reaches the desktop at (400, 0): not repainted */
  else{
   CHECK(rc==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+8);
   if(k==2)CHECK(rd(&r,up+2,4)==0 && rd(&r,up+6,4)==0);                                         /* entirely off the screen: nothing to update */
   else CHECK(rd(&r,up+2,2)==50 && rd(&r,up+4,2)==60 && rd(&r,up+6,2)==80 && rd(&r,up+8,2)==100);   /* the L's bounding box, which is the update area */
  }
  mac_cpu_destroy(r.cpu);
 }
 /* a complex region of the wrong kind of handle (not one of ours) is refused as before */
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);wr(&r,0x9da,2,1);
 uint32_t h=region_args(&r,50,60,60,100),p=rd(&r,h,4);wr(&r,p,2,22);CHECK(paint(&r,h)==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 /* the desktop region InitWindows publishes (GrayRgn, $9EE): the screen below the menu bar, a region of ours */
 r=fresh();uint32_t g=rd(&r,0x9ee,4);CHECK(g && rd(&r,g,4));{uint32_t gp=rd(&r,g,4);CHECK(rd(&r,gp,2)==10&&rd(&r,gp+2,2)==20&&rd(&r,gp+4,2)==0&&rd(&r,gp+6,2)==480&&rd(&r,gp+8,2)==640);}
 {uint32_t x=region_args(&r,0,0,100,100);mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,x);wr(&r,SP+4,4,x);wr(&r,SP+8,4,g);CHECK(mac_regions_service(&r,0xa8e4)==1);      /* SectRgn(GrayRgn, x, x) works */
  uint32_t xp=rd(&r,x,4);CHECK(rd(&r,xp+2,2)==20&&rd(&r,xp+6,2)==100&&rd(&r,xp+8,2)==100);}
 mac_cpu_destroy(r.cpu);
}
static void failures(void){for(unsigned kind=0;kind<5;kind++){
 MacRuntime r=fresh();new_args(&r,STORAGE,1);CHECK(mac_window_service(&r,0xaa45)==1);
 CHECK(call(&r,0xa922,STORAGE)==1 && call(&r,0xa923,STORAGE)==1);
 uint32_t up=rd(&r,r.window.handles[10],4),h=region_args(&r,50,60,70,80),p=rd(&r,h,4);wr(&r,0x9da,2,1);
 if(kind==0)wr(&r,p,2,12);
 if(kind==1)wr(&r,h,4,p+2);
 if(kind==2)wr(&r,STORAGE+144,4,0x7770);
 if(kind==4)wr(&r,p+4,2,0); /* Visible desktop left of the window. */
 if(kind==3){wr(&r,up+2,2,100);wr(&r,up+4,2,100);wr(&r,up+6,2,110);wr(&r,up+8,2,110);}
 uint8_t before[307200],update[10];memcpy(before,mac_cpu_ram(r.cpu)+r.screen_base,sizeof before);memcpy(update,mac_cpu_ram(r.cpu)+up,10);
 CHECK(paint(&r,h)==-1 && mac_cpu_get(r.cpu,MAC_A7)==SP);
 CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+r.screen_base,sizeof before) && !memcmp(update,mac_cpu_ram(r.cpu)+up,10));mac_cpu_destroy(r.cpu);
}}
static void plain_dialog(void){
 MacRuntime r=fresh();new_args(&r,STORAGE,0);wr(&r,SP+10,2,2);
 CHECK(mac_window_service(&r,0xaa45)==1 && r.window.proc_id==2 && !r.window.visible);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+39*640+49]==77);
 uint32_t structure=rd(&r,r.window.handles[8],4);
 CHECK(rd(&r,structure+2,2)==39 && rd(&r,structure+4,2)==49);
 CHECK(call(&r,0xa915,STORAGE)==1);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+39*640+49]==255);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+38*640+100]==77);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+40*640+50]==0);
 CHECK(mac_window_hit(&r,100,100)==3 && mac_window_hit(&r,100,39)==0);
 CHECK(call(&r,0xa80a,STORAGE)==1 && rd(&r,SP+4,2)==2);
 /* Closing restores only the actual one-pixel frame and content. */
 mac_cpu_ram(r.cpu)[r.screen_base+30*640+100]=42;
 CHECK(call(&r,0xa92d,STORAGE)==1);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+39*640+49]==77);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+30*640+100]==42);mac_cpu_destroy(r.cpu);
}
int main(void){complex_region();repaint();failures();plain_dialog();puts("mac PaintBehind and plainDBox tests passed");return 0;}
