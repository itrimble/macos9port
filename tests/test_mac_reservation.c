#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x100000;r.heap_limit=0x200000;r.qd_globals=0x7000;wr(&r,0x8000,4,0x9000);mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_color_port_service(&r,0xaa00)==1);return r;}
static int reserve(MacRuntime*r,int index,int flag){wr(r,0x8000,2,(flag?0x0100:0)|0x7f);wr(r,0x8002,2,(uint16_t)index);mac_cpu_set(r->cpu,MAC_A7,0x8000);return mac_palette_service(r,0xaa3e);}
static int rgb(MacRuntime*r){wr(r,0x8000,4,0x6000);mac_cpu_set(r->cpu,MAC_A7,0x8000);return mac_color_raster_service(r,0xaa14);}
static void success(void){
 MacRuntime r=fresh();uint32_t gd=rd(&r,r.main_device,4),ct=rd(&r,r.screen_colors,4),entry=ct+16;
 wr(&r,gd+2,2,7);wr(&r,entry,2,0x8000);uint32_t seed=rd(&r,ct,4);
 CHECK(reserve(&r,1,1)==1 && rd(&r,entry,2)==0x8007 && rd(&r,ct,4)!=seed);
 CHECK(mac_cpu_get(r.cpu,MAC_A7)==0x8004 && rd(&r,0x8000,2)==0x017f);
 memcpy(mac_cpu_ram(r.cpu)+0x6000,mac_cpu_ram(r.cpu)+entry+2,6);
 CHECK(rgb(&r)==1 && rd(&r,0x9000+80,4)==1);
 wr(&r,gd+2,2,8);CHECK(rgb(&r)==1 && rd(&r,0x9000+80,4)!=1);
 CHECK(reserve(&r,1,0)==1 && rd(&r,entry,2)==0x8000); /* false ignores dirty padding */
 CHECK(rgb(&r)==1 && rd(&r,0x9000+80,4)==1);
 mac_cpu_destroy(r.cpu);
}
static void failure(void){for(unsigned k=0;k<3;k++){
 MacRuntime r=fresh();uint32_t gd=rd(&r,r.main_device,4),ct=rd(&r,r.screen_colors,4);wr(&r,gd+2,2,7);wr(&r,ct+16,2,8);
 uint8_t before[2056];memcpy(before,mac_cpu_ram(r.cpu)+ct,sizeof before);
 if(k==2)wr(&r,r.screen_colors,4,MAC_RUNTIME_RAM-2);
 CHECK(reserve(&r,k==1?256:1,1)==(k==2?-1:1));
 CHECK(mac_cpu_get(r.cpu,MAC_A7)==(k==2?0x8000:0x8004));
 if(k<2){CHECK(!mac_cpu_halted(r.cpu));CHECK(r.qd_error==(k==1?-153:-154));
 mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_palette_service(&r,0xaa40)==1);
 CHECK((int16_t)rd(&r,0x8000,2)==r.qd_error && mac_cpu_get(r.cpu,MAC_A7)==0x8000);
 CHECK(reserve(&r,1,0)==1 && r.qd_error==0);wr(&r,ct+16,2,8);
 /* Successful unreserve increments the seed, so restore it for snapshot check. */
 memcpy(mac_cpu_ram(r.cpu)+ct,before,4);}
 CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+ct,sizeof before));mac_cpu_destroy(r.cpu);
}}
int main(void){success();failure();puts("mac reservation tests passed");return 0;}
