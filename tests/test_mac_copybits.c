#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_copybits_service(MacRuntime*,uint16_t);
int mac_regions_service(MacRuntime*,uint16_t);
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
static void faults(void){for(unsigned test=0;test<4;test++){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x5000,0xa7,32);
 if(test==0)wr(&r,0x1000,4,65530);
 if(test==1)wr(&r,SP+4,2,32);
 if(test==2){wr(&r,SP,4,0x3000);wr(&r,0x3000,4,0x3010);wr(&r,0x3010,2,12);}
 if(test==3)mac_cpu_set(r.cpu,MAC_A7,0xfffffffe);
 if(test==4){wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);wr(&r,0x320a,2,10);}
 CHECK(mac_copybits_service(&r,0xa8ec)==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0xa7);mac_cpu_destroy(r.cpu);
}}
/* A palette-linked source table (ctFlags bit 14): pixel -> table entry -> palette index (the entry's value) -> device index. */
static void palette_linked(void){for(unsigned t=0;t<12;t++){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);
 wr(&r,0x1000+42,4,0x3400);wr(&r,0x3400,4,0x3500);wr(&r,0x3500,4,0x77);wr(&r,0x3504,2,0x4000);wr(&r,0x3506,2,255);
 for(unsigned j=0;j<256;j++){wr(&r,0x3508+j*8,2,(j*3)%64);wr(&r,0x350a+j*8,2,0xabcd);}   /* rgb differs from the destination table and must be ignored */
 wr(&r,0x3e00,4,0x3e10);wr(&r,0x3e10,2,64);r.palette_active=0x3e00;for(unsigned v=0;v<64;v++)r.palette_map[v]=(uint8_t)(100+v);
 for(unsigned i=0;i<32;i++)b[0x4000+i]=(unsigned char)i;memset(b+0x5000,0xa7,32);
 if(t==1)wr(&r,SP+4,2,1);                    /* not srcCopy */
 if(t==2)r.palette_active=0;                 /* no activated palette */
 if(t==3)wr(&r,0x3508+5*8,2,64);             /* pixel 5's entry points past the palette */
 if(t==4)wr(&r,0x3504,2,0x4001);             /* a reserved flag bit */
 if(t==5)wr(&r,0x3504,2,0x6000);             /* another reserved bit */
 if(t==6)wr(&r,0x2104,2,0x4000);             /* the destination table is palette-linked */
 if(t==7)wr(&r,0x3506,2,3);                  /* the table is too short for pixel 5 */
 if(t==8)wr(&r,0x3e10,2,5);                  /* a palette too small for the entries pixels use */
 if(t==9)wr(&r,0x3e00,4,0xfffff0);           /* the palette handle is outside RAM */
 if(t==10)wr(&r,0x3504,2,0xc000);            /* device table bit together with the link bit: still linked */
 if(t==11){wr(&r,0x3e10,2,300);wr(&r,0x3508+5*8,2,280);}   /* an index above 255 cannot be a device index */
 int rc=mac_copybits_service(&r,0xa8ec);
 if(t==0||t==10){CHECK(rc==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+22);for(unsigned i=0;i<32;i++)CHECK(b[0x5000+i]==(unsigned char)(100+(i*3)%64));}
 else{CHECK(rc==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0xa7);}   /* a halt leaves the destination alone */
 mac_cpu_destroy(r.cpu);
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
/* Different colour tables: srcCopy maps every source pixel to the destination entry of the same colour (nearest by squared RGB, lowest index on a tie); other modes still halt. */
static void conversion(void){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x5000,0xa7,32);
 wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);wr(&r,0x320a,2,10);       /* destination: seed 1, entry 0 is (10,0,0), the rest black */
 CHECK(mac_copybits_service(&r,0xa8ec)==1);for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==1);                         /* source pixel 0 is black: nearest is entry 1, not 0 */
 mac_cpu_destroy(r.cpu);
 r=setup(8);b=mac_cpu_ram(r.cpu);memset(b+0x4000,5,32);memset(b+0x5000,0xa7,32);
 wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);
 for(unsigned j=0;j<256;j++){wr(&r,0x2100+8+j*8+2,2,(uint16_t)(j*200));wr(&r,0x3200+8+j*8+2,2,(uint16_t)((255-j)*200));}  /* the two tables run in opposite directions */
 CHECK(mac_copybits_service(&r,0xa8ec)==1);for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==250);                       /* colour of source entry 5 is the destination's entry 250 */
 mac_cpu_destroy(r.cpu);
 r=setup(8);b=mac_cpu_ram(r.cpu);memset(b+0x5000,0xa7,32);
 wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);wr(&r,0x320a,2,10);wr(&r,SP+4,2,1);
 CHECK(mac_copybits_service(&r,0xa8ec)==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0xa7);mac_cpu_destroy(r.cpu);
}
/* Colour conversion picks the nearest entry by squared distance (not by the sum of differences), in CopyBits and in CopyMask. */
static void nearest_and_mask_conversion(void){
 for(unsigned which=0;which<2;which++){
  MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x5000,0xa7,32);
  wr(&r,0x1100+42,4,0x3100);wr(&r,0x3100,4,0x3200);wr(&r,0x3200,4,1);wr(&r,0x3206,2,255);
  for(unsigned j=0;j<256;j++){wr(&r,0x3200+8+j*8+2,2,0xffff);wr(&r,0x3200+8+j*8+4,2,0xffff);wr(&r,0x3200+8+j*8+6,2,0xffff);}
  wr(&r,0x3200+8+3*8+2,2,180);wr(&r,0x3200+8+3*8+4,2,0);wr(&r,0x3200+8+3*8+6,2,0);                                   /* entry 3: (180,0,0) */
  wr(&r,0x3200+8+4*8+2,2,100);wr(&r,0x3200+8+4*8+4,2,100);wr(&r,0x3200+8+4*8+6,2,0);                                 /* entry 4: (100,100,0) */
  if(which==0)CHECK(mac_copybits_service(&r,0xa8ec)==1);                                                                /* source pixel 0 is black: squared distance picks 4 (20000 < 32400) */
  else{enum{MP=0x1400};wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,8);memset(b+0x6000,0xff,8);
   wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);rect(&r,0x1500,0,0,4,8);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
   CHECK(mac_copybits_service(&r,0xa817)==1);}
  for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==4);mac_cpu_destroy(r.cpu);
 }
}
/* A source rectangle past the source bitmap's bounds is cut back to them and the destination shrinks with it; nothing is read outside the source. */
static void crop(void){
 for(unsigned which=0;which<2;which++){
  MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++){b[0x4000+i]=(unsigned char)(10+i);b[0x5000+i]=0x77;}
  memset(b+0x4020,0xee,64);                                                          /* bytes after the source: must never be copied */
  rect(&r,0x1200,2,6,6,10);                                                          /* rows 2..5, columns 6..9; the bitmap is 4 rows x 8 columns */
  rect(&r,0x1300,2,6,6,10);                                                          /* the same place in the destination */
  if(which==1){enum{MP=0x1400};wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,8,16);memset(b+0x6000,0xff,16);rect(&r,0x1500,2,6,6,10);
   wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);}
  rect(&r,0x1100+6,0,0,8,16);                                                        /* a destination large enough to hold the target rectangle */
  wr(&r,0x1100+4,2,0x8010);memset(b+0x5000,0x77,128);
  CHECK(mac_copybits_service(&r,which?0xa817:0xa8ec)==1&&!mac_cpu_halted(r.cpu));
  CHECK(b[0x5000+2*16+6]==10+2*8+6&&b[0x5000+2*16+7]==10+2*8+7);                      /* rows 2 and 3, columns 6 and 7: inside the source */
  CHECK(b[0x5000+3*16+6]==10+3*8+6&&b[0x5000+3*16+7]==10+3*8+7);
  CHECK(b[0x5000+2*16+8]==0x77&&b[0x5000+2*16+9]==0x77&&b[0x5000+4*16+6]==0x77&&b[0x5000+5*16+7]==0x77);   /* outside the source: untouched */
  mac_cpu_destroy(r.cpu);
 }
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x5000,0x77,32);rect(&r,0x1200,10,10,12,12);               /* entirely outside the source: nothing happens, and it is not an error */
 CHECK(mac_copybits_service(&r,0xa8ec)==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+22);for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0x77);mac_cpu_destroy(r.cpu);
}
/* A complex mask region (and a complex visRgn or clipRgn on the port) narrows CopyBits and CopyMask pixel by pixel. */
static uint32_t new_rgn(MacRuntime*r){mac_cpu_set(r->cpu,MAC_A7,0x7000);CHECK(mac_regions_service(r,0xa8d8)==1);return (uint32_t)mac_cpu_read(r->cpu,0x7000,4);}
static void rect_rgn(MacRuntime*r,uint32_t h,int t,int l,int b,int x){rect(r,0x7100,t,l,b,x);mac_cpu_set(r->cpu,MAC_A7,0x7000);wr(r,0x7000,4,0x7100);wr(r,0x7004,4,h);CHECK(mac_regions_service(r,0xa8df)==1);}
static uint32_t l_shape(MacRuntime*r){                      /* rows 0-1 all 8 columns, rows 2-3 columns 0-3: not a rectangle */
 uint32_t a=new_rgn(r),b=new_rgn(r),c=new_rgn(r);rect_rgn(r,a,0,0,2,8);rect_rgn(r,b,2,0,4,4);
 mac_cpu_set(r->cpu,MAC_A7,0x7000);wr(r,0x7000,4,c);wr(r,0x7004,4,b);wr(r,0x7008,4,a);CHECK(mac_regions_service(r,0xa8e5)==1);return c;
}
static void complex_regions(void){
 for(unsigned which=0;which<3;which++){
  MacRuntime r=setup(8);r.heap_next=0x9000;r.heap_limit=0xf000;unsigned char*b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++){b[0x4000+i]=(unsigned char)(10+i);b[0x5000+i]=0x77;}
  uint32_t l=l_shape(&r);mac_cpu_set(r.cpu,MAC_A7,SP);
  if(which==0)wr(&r,SP,4,l);                                                         /* the maskRgn argument of CopyBits */
  else{
   uint32_t port=0x6800;r.current_port=port;r.qd_globals=0x6f00;wr(&r,0x6f00,4,port);wr(&r,port+6,2,0xc000);wr(&r,port+2,4,0x6900);wr(&r,0x6900,4,0x1100);
   wr(&r,port+36,4,0);wr(&r,port+40,2,0);wr(&r,port+42,4,0xffffffffu);wr(&r,port+46,2,0xffff);
   uint32_t whole=new_rgn(&r);rect_rgn(&r,whole,-100,-100,100,100);
   wr(&r,port+24,4,which==1?l:whole);wr(&r,port+28,4,which==1?whole:l);               /* visRgn or clipRgn is the L */
   mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0);
  }
  wr(&r,SP+4,2,0);wr(&r,SP+6,4,0x1300);wr(&r,SP+10,4,0x1200);wr(&r,SP+14,4,0x1100);wr(&r,SP+18,4,0x1000);
  CHECK(mac_copybits_service(&r,0xa8ec)==1&&!mac_cpu_halted(r.cpu));
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++){unsigned in=y<2||x<4;CHECK(b[0x5000+y*8+x]==(in?10+y*8+x:0x77));}
  mac_cpu_destroy(r.cpu);
 }
 /* CopyMask through a complex clip region */
 MacRuntime r=setup(8);r.heap_next=0x9000;r.heap_limit=0xf000;unsigned char*b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++){b[0x4000+i]=(unsigned char)(10+i);b[0x5000+i]=0x77;}
 uint32_t l=l_shape(&r);uint32_t port=0x6800;r.current_port=port;r.qd_globals=0x6f00;wr(&r,0x6f00,4,port);wr(&r,port+6,2,0xc000);wr(&r,port+2,4,0x6900);wr(&r,0x6900,4,0x1100);
 wr(&r,port+42,4,0xffffffffu);wr(&r,port+46,2,0xffff);uint32_t whole=new_rgn(&r);rect_rgn(&r,whole,-100,-100,100,100);wr(&r,port+24,4,l);wr(&r,port+28,4,whole);
 enum{MP=0x1400};wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,8);memset(b+0x6000,0xff,8);rect(&r,0x1500,0,0,4,8);
 mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
 CHECK(mac_copybits_service(&r,0xa817)==1);for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++){unsigned in=y<2||x<4;CHECK(b[0x5000+y*8+x]==(in?10+y*8+x:0x77));}
 mac_cpu_destroy(r.cpu);
 /* a complex mask handle that is not a region of ours is refused, not read */
 r=setup(8);b=mac_cpu_ram(r.cpu);memset(b+0x5000,0x77,32);wr(&r,SP,4,0x3000);wr(&r,0x3000,4,0x3010);wr(&r,0x3010,2,22);
 CHECK(mac_copybits_service(&r,0xa8ec)==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0x77);mac_cpu_destroy(r.cpu);
}
/* A colour port's portBits address (port+2) names the port's PixMap handle, flagged by the port's portVersion word ($C000) where rowBytes would be. */
static void port_bits(void){
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);memset(b+0x4000,0x42,32);memset(b+0x5000,0,32);
 wr(&r,0x7000+2,4,0x7100);wr(&r,0x7000+6,2,0xc000);wr(&r,0x7100,4,0x1100);                                           /* port at 0x7000: handle 0x7100 -> PixMap at 0x1100 */
 wr(&r,SP+14,4,0x7000+2);
 CHECK(mac_copybits_service(&r,0xa8ec)==1);for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0x42);mac_cpu_destroy(r.cpu);
 for(unsigned k=0;k<4;k++){
  r=setup(8);b=mac_cpu_ram(r.cpu);wr(&r,0x7000+2,4,k==0?0:0x7100);wr(&r,0x7000+6,2,0xc000);wr(&r,0x7100,4,k==1?0:k==2?0xfffff0:0x7000);wr(&r,SP+14,4,0x7000+2);
  if(k==3){wr(&r,0x7100,4,0x7000);}                                                                                   /* the PixMap pointer is the port itself: refused */
  CHECK(mac_copybits_service(&r,0xa8ec)==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 }
}
/* CopyMask(srcBits, maskBits, dstBits; srcRect, maskRect, dstRect): srcCopy where the 1-bit mask is set. */
static void copymask(void){
 #define MASK 0x1400
 enum{MP=0x1400};
 MacRuntime r=setup(8);unsigned char*b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++){b[0x4000+i]=(unsigned char)(10+i);b[0x5000+i]=0x77;}
 wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,8);b[0x6000]=0xa5;b[0x6002]=0xff;b[0x6004]=0x00;b[0x6006]=0x0f;b[0x6001]=b[0x6003]=b[0x6005]=b[0x6007]=0;
 wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);rect(&r,0x1500,0,0,4,8);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
 CHECK(mac_copybits_service(&r,0xa817)==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+24);
 for(unsigned x=0;x<8;x++){CHECK(b[0x5000+x]==(((0xa5>>(7-x))&1)?10+x:0x77));CHECK(b[0x5008+x]==10+8+x);CHECK(b[0x5010+x]==0x77);CHECK(b[0x5018+x]==((x>=4)?10+24+x:0x77));}
 mac_cpu_destroy(r.cpu);
 /* clipped like CopyBits: destination rectangle and the mask map's own bounds are enforced; refusals leave the destination alone */
 for(unsigned k=0;k<8;k++){
  r=setup(8);b=mac_cpu_ram(r.cpu);memset(b+0x5000,0x77,32);wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,8);memset(b+0x6000,0xff,8);
  wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);rect(&r,0x1500,0,0,4,8);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
  if(k==0)wr(&r,MP+4,2,8);                                                          /* the mask is not 1-bit... (rowBytes 8 is still 1-bit: use a PixMap flag instead) */
  if(k==0){wr(&r,MP+4,2,0x8008);wr(&r,MP+32,2,8);wr(&r,MP+34,2,1);wr(&r,MP+36,2,8);wr(&r,MP+42,4,0x2000);}
  if(k==1)wr(&r,SP+20,4,0);                                                         /* NIL source bitmap */
  if(k==2)rect(&r,0x1500,0,0,4,9);                                                  /* mask rectangle past the mask's bounds */
  if(k==3)wr(&r,SP+4,4,0);                                                          /* NIL mask rectangle */
  if(k==4)wr(&r,SP+8,4,0);                                                          /* NIL source rectangle */
  if(k==5)mac_cpu_set(r.cpu,MAC_A7,0xfffffffe);
  if(k==6){wr(&r,0x1000+4,2,2);rect(&r,0x1000+6,0,0,4,8);}                          /* source 1-bit, destination 8-bit: mixed depths */
  if(k==7)wr(&r,SP+16,4,0xfffff0);                                                  /* the mask bitmap pointer is outside RAM */
  CHECK(mac_copybits_service(&r,0xa817)==-1&&mac_cpu_halted(r.cpu));for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0x77);mac_cpu_destroy(r.cpu);
 }
 /* a mask larger than the destination rectangle is sampled proportionally; an empty mask rectangle changes nothing */
 r=setup(8);b=mac_cpu_ram(r.cpu);for(unsigned i=0;i<32;i++){b[0x4000+i]=(unsigned char)(10+i);b[0x5000+i]=0x77;}
 wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,16);memset(b+0x6000,0,8);b[0x6000]=0xa0;                      /* a 16-wide mask with its pixels 0 and 2 set */
 wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);rect(&r,0x1500,0,0,4,16);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
 CHECK(mac_copybits_service(&r,0xa817)==1);CHECK(b[0x5000]==10&&b[0x5001]==11&&b[0x5002]==0x77);mac_cpu_destroy(r.cpu);   /* destination pixel 1 samples mask pixel 2 */
 r=setup(8);b=mac_cpu_ram(r.cpu);memset(b+0x5000,0x77,32);wr(&r,MP,4,0x6000);wr(&r,MP+4,2,2);rect(&r,MP+6,0,0,4,8);memset(b+0x6000,0xff,8);
 wr(&r,SP,4,0x1300);wr(&r,SP+4,4,0x1500);rect(&r,0x1500,2,2,2,2);wr(&r,SP+8,4,0x1200);wr(&r,SP+12,4,0x1100);wr(&r,SP+16,4,MP);wr(&r,SP+20,4,0x1000);
 CHECK(mac_copybits_service(&r,0xa817)==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+24);for(unsigned j=0;j<32;j++)CHECK(b[0x5000+j]==0x77);mac_cpu_destroy(r.cpu);
 r=setup(8);CHECK(mac_copybits_service(&r,0xa818)==0);mac_cpu_destroy(r.cpu);
}
int main(void){complex_regions();crop();conversion();nearest_and_mask_conversion();port_bits();copymask();palette_linked();equal_seed();modes();overlap_scale_mask();port_clip();faults();puts("mac CopyBits tests passed");return 0;}
