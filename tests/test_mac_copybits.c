#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_copybits_service(MacRuntime*,uint16_t);
#define CHECK(c) do{if(!(c)){fprintf(stderr,"line%d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define SP 0x8000u
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static void rect(MacRuntime*r,uint32_t p,int t,int l,int b,int x){wr(r,p,2,t);wr(r,p+2,2,l);wr(r,p+4,2,b);wr(r,p+6,2,x);}
static MacRuntime setup(int depth){
 MacRuntime r={0};r.cpu=mac_cpu_create(65536);CHECK(r.cpu);mac_cpu_set(r.cpu,MAC_A7,SP);
 for(unsigned p=0x1000;p<=0x1100;p+=0x100){wr(&r,p,4,p==0x1000?0x4000:0x5000);wr(&r,p+4,2,depth==8?0x8008:2);rect(&r,p+6,0,0,4,8);
 if(depth==8){wr(&r,p+32,2,8);wr(&r,p+34,2,1);wr(&r,p+36,2,8);wr(&r,p+42,4,0x2000);}}
 wr(&r,0x2000,4,0x2100);wr(&r,0x2106,2,255);
 rect(&r,0x1200,0,0,4,8);rect(&r,0x1300,0,0,4,8);
 wr(&r,SP,4,0);wr(&r,SP+4,2,0);wr(&r,SP+6,4,0x1300);wr(&r,SP+10,4,0x1200);wr(&r,SP+14,4,0x1100);wr(&r,SP+18,4,0x1000);return r;
}
static void modes(void){for(unsigned m=0;m<8;m++)for(int depth=1;depth<=8;depth+=7){
 MacRuntime r=setup(depth);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x4000,0xa5,32);memset(b+0x5000,0x3c,32);wr(&r,SP+4,2,m);
 CHECK(mac_copybits_service(&r,0xa8ec)==1);CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+22);
 unsigned s=(m&4)?0x5a:0xa5,d=0x3c,v;switch(m&3){case 0:v=s;break;case 1:v=s|d;break;case 2:v=s^d;break;default:v=(~s)&d;break;}
 CHECK(b[0x5000]==(unsigned char)v);if(depth==1)CHECK(b[0x5001]==0x3c);mac_cpu_destroy(r.cpu);
}}
static void overlap_scale_mask(void){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++)b[0x4000+i]=i;
 wr(&r,0x1100,4,0x4000);rect(&r,0x1200,0,0,1,6);rect(&r,0x1300,0,2,1,8);
 CHECK(mac_copybits_service(&r,0xa8ec)==1);for(unsigned i=0;i<6;i++)CHECK(b[0x4002+i]==i);mac_cpu_destroy(r.cpu);
 r=setup(8);b=mac_cpu_ram(r.cpu);b[0x4000]=10;b[0x4001]=20;rect(&r,0x1200,0,0,1,2);rect(&r,0x1300,0,0,2,4);
 wr(&r,SP,4,0x3000);wr(&r,0x3000,4,0x3010);wr(&r,0x3010,2,10);rect(&r,0x3012,0,1,2,3);
 CHECK(mac_copybits_service(&r,0xa8ec)==1);CHECK(b[0x5000]==0&&b[0x5001]==10&&b[0x5002]==20&&b[0x5003]==0);CHECK(b[0x5009]==10&&b[0x500a]==20);mac_cpu_destroy(r.cpu);
}
static void port_clip(void){
 MacRuntime r=setup(1);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x4000,255,8);
 r.current_port=0x6000;memcpy(b+0x6002,b+0x1100,14);
 wr(&r,0x6000+80,4,33);wr(&r,0x6000+84,4,30);
 wr(&r,0x6000+24,4,0x3000);wr(&r,0x3000,4,0x3010);wr(&r,0x3010,2,10);rect(&r,0x3012,0,0,4,8);
 wr(&r,0x6000+28,4,0x3020);wr(&r,0x3020,4,0x3030);wr(&r,0x3030,2,10);rect(&r,0x3032,1,2,3,6);
 CHECK(mac_copybits_service(&r,0xa8ec)==1);CHECK(b[0x5000]==0&&b[0x5002]==0x3c&&b[0x5004]==0x3c&&b[0x5006]==0);
 mac_cpu_destroy(r.cpu);
}
static void faults(void){for(unsigned test=0;test<5;test++){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x5000,0xa7,32);
 if(test==0)wr(&r,0x1000,4,65530);
 if(test==1)wr(&r,SP+4,2,32);
 if(test==2){wr(&r,SP,4,0x3000);wr(&r,0x3000,4,0x3010);wr(&r,0x3010,2,12);}
 if(test==3)mac_cpu_set(r.cpu,MAC_A7,0xfffffffe);
 if(test==4){wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);wr(&r,0x320a,2,10);}
 CHECK(mac_copybits_service(&r,0xa8ec)==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0xa7);mac_cpu_destroy(r.cpu);
}}
static void equal_seed(void){for(unsigned test=0;test<5;test++) {
 MacRuntime r=setup(8);uint8_t *b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++)b[0x4000+i]=(uint8_t)(255-i);
 memset(b+0x5000,0xa7,32);
 /* Put an eight-byte empty source table exactly at the end of guest RAM.
  * An equal-seed srcCopy must never read nonexistent ColorSpec entries. */
 wr(&r,0x2000,4,65528);wr(&r,65528,4,1024);wr(&r,65534,2,0xffff);
 wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1024);wr(&r,0x3206,2,255);
 if(test==1)wr(&r,0x3200,4,1025);
 if(test==2)wr(&r,SP+4,2,1);
 if(test==3)wr(&r,0x2000,4,65530);
 if(test==4)wr(&r,65532,2,0x4000);
 int result=mac_copybits_service(&r,0xa8ec);
 if(!test){CHECK(result==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+22);CHECK(!memcmp(b+0x4000,b+0x5000,32));}
 else {CHECK(result==-1 && mac_cpu_halted(r.cpu));for(unsigned i=0;i<32;i++)CHECK(b[0x5000+i]==0xa7);CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);}
 mac_cpu_destroy(r.cpu);
}}
int main(void){equal_seed();modes();overlap_scale_mask();port_clip();faults();puts("mac CopyBits tests passed");return 0;}
