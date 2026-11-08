#include "mac_runtime.h"
#include "mac_pict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_picture_service(MacRuntime*,uint16_t);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct{uint8_t b[8192];size_t n;} Writer;
static void byte(Writer*w,unsigned v){CHECK(w->n<sizeof w->b);w->b[w->n++]=(uint8_t)v;}
static void word(Writer*w,unsigned v){byte(w,v>>8);byte(w,v);}
static void lng(Writer*w,uint32_t v){word(w,v>>16);word(w,v);}
static void rect(Writer*w,int t,int l,int b,int r){word(w,t);word(w,l);word(w,b);word(w,r);}
static void align(Writer*w){if(w->n&1)byte(w,0);}
static void begin(Writer*w,unsigned width,unsigned height){memset(w,0,sizeof *w);word(w,0);rect(w,0,0,height,width);word(w,0x11);word(w,0x2ff);word(w,0xc00);lng(w,UINT32_MAX);lng(w,0);lng(w,0);lng(w,width<<16);lng(w,height<<16);lng(w,0);}
/* An extended version 2 header (Apple's Imaging With QuickDraw, Appendix A, Listing A-5): Version -2, reserved, resolutions as Fixed, source rectangle, reserved long. */
static void begin_ext(Writer*w,unsigned width,unsigned height,uint32_t hres,uint32_t vres,int t,int l,int b,int r,unsigned version,unsigned reserved,uint32_t reserved2){
 memset(w,0,sizeof *w);word(w,0);rect(w,0,0,height,width);word(w,0x11);word(w,0x2ff);word(w,0xc00);
 word(w,version);word(w,reserved);lng(w,hres);lng(w,vres);rect(w,t,l,b,r);lng(w,reserved2);
}
static void band(Writer*w,unsigned width,unsigned top,unsigned height,int repeat,int sparse){
 align(w);word(w,0x98);word(w,0x8000|width);rect(w,top,0,top+height,width);
 word(w,0);word(w,0);lng(w,0);lng(w,72u<<16);lng(w,72u<<16);
 word(w,0);word(w,8);word(w,1);word(w,8);lng(w,0);lng(w,0);lng(w,0);
 lng(w,123);word(w,sparse?0:0x8000);word(w,1);
 word(w,sparse?5:0);word(w,65535);word(w,0);word(w,0);
 word(w,sparse?7:0);word(w,0);word(w,65535);word(w,0);
 rect(w,top,0,top+height,width);rect(w,top,0,top+height,width);word(w,0);
 for(unsigned y=0;y<height;y++){
  if(width<8){for(unsigned x=0;x<width;x++)byte(w,x<width/2?(sparse?5:0):(sparse?7:1));}
  else if(width>250){word(w,4);byte(w,129);byte(w,0);byte(w,129);byte(w,1);}
  else if(repeat){byte(w,3);byte(w,128);byte(w,1-width);byte(w,sparse?7:1);}
  else{byte(w,width+1);byte(w,width-1);for(unsigned x=0;x<width;x++)byte(w,x<width/2?(sparse?5:0):(sparse?7:1));}
 }
}
static void end(Writer*w){align(w);word(w,0xff);w->b[0]=(uint8_t)(w->n>>8);w->b[1]=(uint8_t)w->n;}
static MacPictPalette colors(void){MacPictPalette p={0};for(unsigned i=0;i<256;i++){p.rgb[i][2]=65535;p.allowed[i]=1;}p.rgb[10][0]=65535;p.rgb[10][2]=0;p.rgb[20][1]=65535;p.rgb[20][2]=0;return p;}
static void decoder(void){
 Writer w;begin(&w,8,2);band(&w,8,0,1,0,0);band(&w,8,1,1,1,0);end(&w);
 MacPictPalette p=colors();MacPictImage i;char error[160];
 CHECK(!mac_pict_decode(w.b,w.n,16,4,&p,&i,error,sizeof error));
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++)CHECK(i.covered[y*16+x]&&i.pixels[y*16+x]==(y<2&&x<8?10:20));mac_pict_destroy(&i);
 p.allowed[10]=0;CHECK(!mac_pict_decode(w.b,w.n,8,2,&p,&i,error,sizeof error));CHECK(i.pixels[0]!=10);mac_pict_destroy(&i);
 for(size_t n=0;n<w.n;n++){CHECK(mac_pict_decode(w.b,n,8,2,&p,&i,error,sizeof error)==-1);CHECK(!i.pixels&&!i.covered);}
 begin(&w,8,1);word(&w,1);word(&w,10);rect(&w,0,2,1,6);band(&w,8,0,1,0,1);end(&w);p=colors();
 CHECK(!mac_pict_decode(w.b,w.n,16,2,&p,&i,error,sizeof error));for(unsigned x=0;x<16;x++)CHECK(i.covered[x]==(x>=4&&x<12));CHECK(i.pixels[4]==10&&i.pixels[8]==20);mac_pict_destroy(&i);
 begin(&w,256,1);band(&w,256,0,1,1,0);end(&w);CHECK(!mac_pict_decode(w.b,w.n,256,1,&p,&i,error,sizeof error));CHECK(i.pixels[127]==10&&i.pixels[128]==20);mac_pict_destroy(&i);
 begin(&w,4,1);band(&w,4,0,1,0,0);end(&w);CHECK(!mac_pict_decode(w.b,w.n,4,1,&p,&i,error,sizeof error));CHECK(i.pixels[0]==10&&i.pixels[3]==20);mac_pict_destroy(&i);
 w.b[w.n-1]=0xfe;CHECK(mac_pict_decode(w.b,w.n,4,1,&p,&i,error,sizeof error)==-1);
}
static void extended_header(void){
 Writer w;MacPictPalette p=colors();MacPictImage i;char error[160];
 begin_ext(&w,8,2,0x480000,0x480000,2,2,110,170,0xfffe,0,0);band(&w,8,0,1,0,0);band(&w,8,1,1,1,0);end(&w);
 CHECK(!mac_pict_decode(w.b,w.n,8,2,&p,&i,error,sizeof error));
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<8;x++)CHECK(i.covered[y*8+x]&&i.pixels[y*8+x]==(y==0&&x<4?10:20));mac_pict_destroy(&i);   /* drawn with the picture frame, source rectangle ignored */
 for(size_t n=0;n<w.n;n++){CHECK(mac_pict_decode(w.b,n,8,2,&p,&i,error,sizeof error)==-1);CHECK(!i.pixels&&!i.covered);}
 static const struct{uint32_t h,v;int t,l,b,r;unsigned ver,res;uint32_t res2;const char*why;}bad[]={
  {0x900000,0x480000,0,0,2,8,0xfffe,0,0,"72 dpi"},{0x480000,0x900000,0,0,2,8,0xfffe,0,0,"72 dpi"},{0x480001,0x480000,0,0,2,8,0xfffe,0,0,"72 dpi"},
  {0x480000,0x480000,2,0,2,8,0xfffe,0,0,"empty"},{0x480000,0x480000,0,5,2,5,0xfffe,0,0,"empty"},
  {0x480000,0x480000,0,0,2,8,0xfffe,1,0,"malformed"},{0x480000,0x480000,0,0,2,8,0xfffe,0,1,"malformed"},{0x480000,0x480000,0,0,2,8,0xfffd,0,0,"malformed"},
  {0x480000,0x480000,0,0,2,8,0x7ffe,0,0,"malformed"}};
 for(unsigned k=0;k<sizeof bad/sizeof*bad;k++){
  begin_ext(&w,8,2,bad[k].h,bad[k].v,bad[k].t,bad[k].l,bad[k].b,bad[k].r,bad[k].ver,bad[k].res,bad[k].res2);band(&w,8,0,1,0,0);band(&w,8,1,1,1,0);end(&w);
  CHECK(mac_pict_decode(w.b,w.n,8,2,&p,&i,error,sizeof error)==-1 && strstr(error,bad[k].why)!=NULL && !i.pixels);
 }
 begin_ext(&w,8,2,0x480000,0x480000,0,0,2,8,0xfffe,0,0);                                   /* a second HeaderOp is refused */
 word(&w,0xc00);word(&w,0xfffe);word(&w,0);lng(&w,0x480000);lng(&w,0x480000);rect(&w,0,0,2,8);lng(&w,0);end(&w);
 CHECK(mac_pict_decode(w.b,w.n,8,2,&p,&i,error,sizeof error)==-1);
}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static void rectangle(MacRuntime*r,uint32_t p,int t,int l,int b,int x){wr(r,p,2,t);wr(r,p+2,2,l);wr(r,p+4,2,b);wr(r,p+6,2,x);}
static MacRuntime setup(Writer*w){
 MacRuntime r={0};r.cpu=mac_cpu_create(65536);CHECK(r.cpu);r.current_port=0x1000;
 wr(&r,0x1002,4,0x1200);wr(&r,0x1006,2,0xc000);wr(&r,0x1200,4,0x1300);
 wr(&r,0x1300,4,0x4000);wr(&r,0x1304,2,0x8014);rectangle(&r,0x1306,-2,-3,10,17);
 wr(&r,0x1320,2,8);wr(&r,0x1322,2,1);wr(&r,0x1324,2,8);wr(&r,0x132a,4,0x2000);wr(&r,0x2000,4,0x2100);
 wr(&r,0x2104,2,0x8000);wr(&r,0x2106,2,255);MacPictPalette p=colors();
 for(unsigned j=0;j<256;j++)for(unsigned c=0;c<3;c++)wr(&r,0x210a+j*8+c*2,2,p.rgb[j][c]);
 rectangle(&r,0x1010,0,0,8,16);wr(&r,0x102a,4,UINT32_MAX);wr(&r,0x102e,2,65535);
 for(unsigned off=24;off<=28;off+=4){uint32_t h=0x3000+(off-24)*16;wr(&r,0x1000+off,4,h);wr(&r,h,4,h+16);wr(&r,h+16,2,10);rectangle(&r,h+18,1,2,5,12);}
 wr(&r,0x5000,4,0x5100);memcpy(mac_cpu_ram(r.cpu)+0x5100,w->b,w->n);
 r.handles[0]=(MacHandle){.handle=0x5000,.pointer=0x5100,.size=(uint32_t)w->n};r.handle_count=1;
 rectangle(&r,0x7000,0,0,4,16);wr(&r,0x8000,4,0x7000);wr(&r,0x8004,4,0x5000);mac_cpu_set(r.cpu,MAC_A7,0x8000);
 memset(mac_cpu_ram(r.cpu)+0x4000,77,240);return r;
}
static void runtime(void){
 Writer w;begin(&w,8,2);band(&w,8,0,2,0,0);end(&w);
 MacRuntime r=setup(&w);CHECK(mac_picture_service(&r,0xa8f6)==1);CHECK(mac_cpu_get(r.cpu,MAC_A7)==0x8008);
 for(int y=-2;y<10;y++)for(int x=-3;x<17;x++){unsigned expected=y>=1&&y<4&&x>=2&&x<12?(x<8?10:20):77;CHECK(mac_cpu_ram(r.cpu)[0x4000+(y+2)*20+x+3]==expected);}mac_cpu_destroy(r.cpu);
 for(unsigned kind=0;kind<4;kind++){
  r=setup(&w);if(kind==0)mac_cpu_ram(r.cpu)[0x5100+w.n-1]=0xfe;
  if(kind==1)wr(&r,0x3010,2,12);
  if(kind==2)wr(&r,0x1300,4,65530);
  if(kind==3)wr(&r,0x5000,4,0);
  CHECK(mac_picture_service(&r,0xa8f6)==-1&&mac_cpu_halted(r.cpu));
  for(unsigned j=0;j<240;j++)CHECK(mac_cpu_ram(r.cpu)[0x4000+j]==77);
  CHECK(mac_cpu_get(r.cpu,MAC_A7)==0x8000);mac_cpu_destroy(r.cpu);
 }
}
static void screen_palette(void){
 Writer w;begin(&w,8,1);band(&w,8,0,1,0,0);end(&w);
 MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x100000;r.heap_limit=0x600000;r.qd_globals=0x7000;
 CHECK(mac_color_init(&r)==1);uint32_t color_port=mac_runtime_alloc(&r,108);
 wr(&r,0x8000,4,color_port);mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_color_port_service(&r,0xaa00)==1);
 uint32_t h=mac_runtime_alloc(&r,4),p=mac_runtime_alloc(&r,(uint32_t)w.n);wr(&r,h,4,p);memcpy(mac_cpu_ram(r.cpu)+p,w.b,w.n);
 r.handles[r.handle_count++]=(MacHandle){.handle=h,.pointer=p,.size=(uint32_t)w.n};
 for(unsigned j=0;j<256;j++)r.palette[j]=0xff0000ff;r.palette[10]=0xffff0000;r.palette[20]=0xff00ff00;
 rectangle(&r,0x7100,40,50,41,58);wr(&r,0x8000,4,0x7100);wr(&r,0x8004,4,h);mac_cpu_set(r.cpu,MAC_A7,0x8000);
 uint8_t port[108];memcpy(port,mac_cpu_ram(r.cpu)+r.current_port,sizeof port);
 CHECK(mac_picture_service(&r,0xa8f6)==1);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+40*640+50]==10 && mac_cpu_ram(r.cpu)[r.screen_base+40*640+57]==20);
 CHECK(!memcmp(port,mac_cpu_ram(r.cpu)+r.current_port,sizeof port));
 uint32_t ct=mac_cpu_read(r.cpu,r.screen_colors,4);wr(&r,ct+8+10*8,2,99);
 mac_cpu_set(r.cpu,MAC_A7,0x8000);CHECK(mac_picture_service(&r,0xa8f6)==1);
 CHECK(mac_cpu_ram(r.cpu)[r.screen_base+40*640+50]!=10);mac_cpu_destroy(r.cpu);
}
int main(void){decoder();extended_header();runtime();screen_palette();puts("PICT indexed playback: ok");return 0;}
