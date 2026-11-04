#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_menu_service(MacRuntime*,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static MacRuntime fresh(int depth){
 MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x10000;r.heap_limit=0x20000;
 r.screen_base=0xe00000;r.screen_width=13;r.screen_height=32;r.screen_depth=(uint16_t)depth;r.screen_row_bytes=depth==1?4:16;
 r.palette[1]=0xffffff;r.palette[0]=0;CHECK(mac_toolbox_service(&r,0xa930)==1);wr(&r,0xbaa,2,3);
 memset(mac_cpu_ram(r.cpu)+r.screen_base,0x5a,128);mac_cpu_set(r.cpu,MAC_A7,0x8000);return r;
}
static void drawing(void){for(int depth=1;depth<=8;depth+=7){
 MacRuntime r=fresh(depth);uint8_t *p=mac_cpu_ram(r.cpu)+r.screen_base;
 CHECK(mac_menu_service(&r,0xa937)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x8000);
 if(depth==8){CHECK(p[0]==1 && p[12]==1 && p[13]==0x5a);CHECK(p[32]==0 && p[44]==0 && p[45]==0x5a);CHECK(p[48]==0x5a);}
 else{CHECK(p[0]==0 && p[1]==2 && p[2]==0x5a);CHECK(p[8]==255 && p[9]==250 && p[10]==0x5a);CHECK(p[12]==0x5a);}
 uint8_t saved[128];memcpy(saved,p,128);wr(&r,0xbaa,2,0);CHECK(mac_menu_service(&r,0xa937)==1 && !memcmp(saved,p,128));
 CHECK(mac_menu_service(&r,0xafff)==0);mac_cpu_destroy(r.cpu);
}}
static void failures(void){for(unsigned k=0;k<6;k++){
 MacRuntime r=fresh(8);uint32_t p=mac_cpu_read(r.cpu,r.menu_bar,4);
 if(k==0)wr(&r,p,2,1);if(k==1)wr(&r,r.menu_bar,4,p+2);if(k==2)wr(&r,0xa1c,4,0);
 if(k==3)wr(&r,0xbaa,2,33);if(k==4)r.screen_row_bytes=12;if(k==5)r.screen_base=MAC_RUNTIME_RAM-2;
 CHECK(mac_menu_service(&r,0xa937)==-1 && mac_cpu_halted(r.cpu));
 for(unsigned i=0;i<128;i++)CHECK(mac_cpu_ram(r.cpu)[0xe00000+i]==0x5a);
 mac_cpu_destroy(r.cpu);
}}
int main(void){drawing();failures();puts("mac menu tests passed");return 0;}
