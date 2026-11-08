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
 CHECK(mac_window_service(&r,0xa915)==-1 && !mac_window_find(&r,windows[0])->visible);mac_cpu_destroy(r.cpu);
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
int main(void){palette_dispatch();dispatched();successful();failure();associations();puts("mac palette manager object and association tests passed");return 0;}
