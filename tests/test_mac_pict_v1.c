/* Version-1 BitsRect/PackBitsRect pictures and DrawPicture into a 1-bit basic GrafPort. */
#include "mac_runtime.h"
#include "mac_pict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_picture_service(MacRuntime*,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct{uint8_t b[512];size_t n;} W;
static void byte(W*w,unsigned v){CHECK(w->n<sizeof w->b);w->b[w->n++]=(uint8_t)v;}
static void word(W*w,unsigned v){byte(w,v>>8);byte(w,v);}
static void rect(W*w,int t,int l,int b,int r){word(w,t);word(w,l);word(w,b);word(w,r);}
/* A version-1 picture: Clip, comment, then one bitmap op (0x90 raw rows, 0x98 PackBits rows). */
static void pict(W*w,unsigned op,unsigned row,int h,int wd,const uint8_t*data,size_t len,unsigned mode){
 memset(w,0,sizeof *w);word(w,0);rect(w,0,0,h,wd);word(w,0x1101);byte(w,0xa0);word(w,0x82);
 byte(w,1);word(w,10);rect(w,0,0,h,wd);
 byte(w,op);word(w,row);rect(w,0,0,h,wd);rect(w,0,0,h,wd);rect(w,0,0,h,wd);word(w,mode);
 for(size_t i=0;i<len;i++)byte(w,data[i]);
 byte(w,0xa0);word(w,0x83);byte(w,0xff);w->b[0]=(uint8_t)(w->n>>8);w->b[1]=(uint8_t)w->n;
}
/* A valid version-2 indexed picture (8x1) whose two colors are neither black nor white. */
static void lng(W*w,uint32_t v){word(w,v>>16);word(w,v);}
static void align(W*w){if(w->n&1)byte(w,0);}
static void v2color(W*w){
 memset(w,0,sizeof *w);word(w,0);rect(w,0,0,1,8);word(w,0x11);word(w,0x2ff);word(w,0xc00);
 lng(w,UINT32_MAX);lng(w,0);lng(w,0);lng(w,8u<<16);lng(w,1u<<16);lng(w,0);
 align(w);word(w,0x98);word(w,0x8008);rect(w,0,0,1,8);word(w,0);word(w,0);lng(w,0);lng(w,72u<<16);lng(w,72u<<16);
 word(w,0);word(w,8);word(w,1);word(w,8);lng(w,0);lng(w,0);lng(w,0);
 lng(w,123);word(w,0x8000);word(w,1);
 word(w,0);word(w,65535);word(w,0);word(w,0);   /* index 0: red   */
 word(w,0);word(w,0);word(w,65535);word(w,0);   /* index 1: green */
 rect(w,0,0,1,8);rect(w,0,0,1,8);word(w,0);
 byte(w,9);byte(w,7);for(unsigned x=0;x<8;x++)byte(w,x<4?0:1);
 align(w);word(w,0xff);w->b[0]=(uint8_t)(w->n>>8);w->b[1]=(uint8_t)w->n;
}
static MacPictPalette bw(void){MacPictPalette p={0};p.exact=1;p.allowed[0]=p.allowed[1]=1;for(unsigned c=0;c<3;c++)p.rgb[0][c]=65535;return p;}
/* 16x4 image: row0 black cols 0-3, row1 black cols 4-15, row2 alternating, row3 white. */
static const uint8_t ROWS[8]={0xf0,0x00,0x0f,0xff,0xaa,0x55,0x00,0x00};
static int black(int x,int y){return (ROWS[y*2+x/8]>>(7-x%8))&1;}
static void decoder(void){
 W w;MacPictImage i;char e[160];MacPictPalette p=bw();
 pict(&w,0x90,2,4,16,ROWS,8,0);CHECK(!mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e));
 for(int y=0;y<4;y++)for(int x=0;x<16;x++)CHECK(i.covered[y*16+x] && i.pixels[y*16+x]==black(x,y));mac_pict_destroy(&i);
 CHECK(!mac_pict_decode(w.b,w.n,32,8,&p,&i,e,sizeof e)); /* 2x scale repeats each source pixel */
 for(int y=0;y<8;y++)for(int x=0;x<32;x++)CHECK(i.pixels[y*32+x]==black(x/2,y/2));mac_pict_destroy(&i);
 /* PackBitsRect with rowBytes 8: one repeat run per row (row <8 would be stored unpacked). */
 uint8_t packed[6]={2,0xf9,0xaa,2,0xf9,0x55};pict(&w,0x98,8,2,64,packed,6,0);
 CHECK(!mac_pict_decode(w.b,w.n,64,2,&p,&i,e,sizeof e));
 for(int x=0;x<64;x++){CHECK(i.pixels[x]==((0xaa>>(7-x%8))&1) && i.pixels[64+x]==((0x55>>(7-x%8))&1));}mac_pict_destroy(&i);
 /* Every truncation fails cleanly and leaves no output. */
 pict(&w,0x90,2,4,16,ROWS,8,0);
 for(size_t n=0;n<w.n;n++){CHECK(mac_pict_decode(w.b,n,16,4,&p,&i,e,sizeof e)==-1 && !i.pixels && !i.covered);}
 /* Refusals: non-srcCopy mode, odd rowBytes, rowBytes too small, unknown opcode, trailing bytes, PixMap flag. */
 pict(&w,0x90,2,4,16,ROWS,8,1);CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 pict(&w,0x90,3,4,16,ROWS,12,0);CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 pict(&w,0x90,2,4,24,ROWS,8,0);CHECK(mac_pict_decode(w.b,w.n,24,4,&p,&i,e,sizeof e)==-1); /* 24 px do not fit 2 bytes */
 pict(&w,0x90,0x8002,4,16,ROWS,8,0);CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 pict(&w,0x91,2,4,16,ROWS,8,0);CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 pict(&w,0x90,2,4,16,ROWS,8,0);w.b[w.n-1]=0xfe;CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 /* A destination with neither exact black nor white cannot take a 1-bit picture. */
 pict(&w,0x90,2,4,16,ROWS,8,0);p=bw();p.allowed[0]=0;CHECK(mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e)==-1);
 /* Colors are refused, not thresholded, when exactness is required; the same picture decodes otherwise. */
 v2color(&w);p=bw();CHECK(mac_pict_decode(w.b,w.n,8,1,&p,&i,e,sizeof e)==-1);
 p=bw();p.exact=0;CHECK(!mac_pict_decode(w.b,w.n,8,1,&p,&i,e,sizeof e));mac_pict_destroy(&i);
 pict(&w,0x90,2,4,16,ROWS,8,0);
 p=bw();p.exact=0;p.allowed[0]=0;p.allowed[7]=1;p.rgb[7][0]=p.rgb[7][1]=p.rgb[7][2]=30000; /* approximate palettes still map */
 CHECK(!mac_pict_decode(w.b,w.n,16,4,&p,&i,e,sizeof e));mac_pict_destroy(&i);
}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static void rc(MacRuntime*r,uint32_t p,int t,int l,int b,int x){wr(r,p,2,t);wr(r,p+2,2,l);wr(r,p+4,2,b);wr(r,p+6,2,x);}
#define BASE 0x4000u
#define RB 4u
/* Basic port: 32x8 bitmap at BASE, local origin (0,0); dst rectangle (2,3)-(6,19) receives the 16x4 picture. */
static MacRuntime setup(const W*w){
 MacRuntime r={0};r.cpu=mac_cpu_create(65536);CHECK(r.cpu);r.current_port=0x1000;r.screen_base=0x9000;
 wr(&r,0x1002,4,BASE);wr(&r,0x1006,2,RB);rc(&r,0x1008,0,0,8,32);rc(&r,0x1010,0,0,8,32);
 for(unsigned off=24;off<=28;off+=4){uint32_t h=0x3000+(off-24)*16;wr(&r,0x1000+off,4,h);wr(&r,h,4,h+16);wr(&r,h+16,2,10);rc(&r,h+18,-100,-100,100,100);}
 wr(&r,0x5000,4,0x5100);memcpy(mac_cpu_ram(r.cpu)+0x5100,w->b,w->n);
 r.handles[0]=(MacHandle){.handle=0x5000,.pointer=0x5100,.size=(uint32_t)w->n};r.handle_count=1;
 rc(&r,0x7000,2,3,6,19);wr(&r,0x8000,4,0x7000);wr(&r,0x8004,4,0x5000);mac_cpu_set(r.cpu,MAC_A7,0x8000);
 memset(mac_cpu_ram(r.cpu)+BASE,0x55,RB*8);return r;
}
static int bit(MacRuntime*r,int x,int y){return (mac_cpu_ram(r->cpu)[BASE+y*RB+x/8]>>(7-x%8))&1;}
static void service(void){
 W w;pict(&w,0x90,2,4,16,ROWS,8,0);MacRuntime r=setup(&w);
 CHECK(mac_picture_service(&r,0xa8f6)==1 && mac_cpu_get(r.cpu,MAC_A7)==0x8008);
 for(int y=0;y<8;y++)for(int x=0;x<32;x++){
  int inside=y>=2&&y<6&&x>=3&&x<19;
  CHECK(bit(&r,x,y)==(inside?black(x-3,y-2):((0x55>>(7-x%8))&1)));
 }
 mac_cpu_destroy(r.cpu);
 /* The clip region limits painting: only columns 8..11 may change. */
 r=setup(&w);rc(&r,0x3000+16+2,-100,8,100,12);CHECK(mac_picture_service(&r,0xa8f6)==1);
 for(int y=0;y<8;y++)for(int x=0;x<32;x++){
  int inside=y>=2&&y<6&&x>=8&&x<12;
  CHECK(bit(&r,x,y)==(inside?black(x-3,y-2):((0x55>>(7-x%8))&1)));
 }
 mac_cpu_destroy(r.cpu);
 /* Refusals halt before any destination byte changes. */
 for(unsigned kind=0;kind<7;kind++){
  W v=w;if(kind==6)v2color(&v);
  r=setup(&v);uint8_t before[RB*8];memcpy(before,mac_cpu_ram(r.cpu)+BASE,sizeof before);
  if(kind==0)wr(&r,0x1068,4,0x6000);              /* grafProcs set */
  if(kind==1)wr(&r,0x1006,2,3);                    /* odd rowBytes */
  if(kind==2)wr(&r,0x1002,4,r.screen_base);       /* the screen bitmap, not an offscreen one */
  if(kind==3)wr(&r,0x1002,4,65530);               /* bitmap beyond RAM */
  if(kind==4)wr(&r,0x1006,2,2);                   /* rowBytes too small for the width */
  if(kind==5)wr(&r,0x5100+(w.n-1),1,0xfe);        /* damaged end marker */
  CHECK(mac_picture_service(&r,0xa8f6)==-1 && mac_cpu_halted(r.cpu));
  CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+BASE,sizeof before) || kind==3);
  mac_cpu_destroy(r.cpu);
 }
}
int main(void){decoder();service();puts("PICT version 1 and 1-bit DrawPicture: ok");return 0;}
