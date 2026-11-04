#include "mac_menu_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void str(MacRuntime*r,const char*s){size_t n=strlen(s);mac_cpu_ram(r->cpu)[0x4000]=(uint8_t)n;memcpy(mac_cpu_ram(r->cpu)+0x4001,s,n);}
static int call(MacRuntime*r,unsigned op){mac_cpu_set(r->cpu,MAC_A7,SP);return mac_menu_service(r,(uint16_t)op);}
static MacRuntime fresh(void){MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);r.heap_next=0x10000;r.heap_limit=0x20000;r.screen_base=0xe00000;r.screen_width=640;r.screen_height=480;r.screen_row_bytes=640;r.screen_depth=8;r.palette[255]=0xffffff;CHECK(mac_toolbox_service(&r,0xa930)==1);return r;}
static uint32_t create(MacRuntime*r,unsigned id,const char*title){str(r,title);wr(r,SP,4,0x4000);wr(r,SP+4,2,id);CHECK(call(r,0xa931)==1);CHECK(mac_cpu_get(r->cpu,MAC_A7)==SP+6);return rd(r,SP+6,4);}
static void resources(void){
 for(unsigned kind=0;kind<7;kind++){
  uint8_t fork[600]={0};fork[2]=1;fork[6]=2;fork[11]=33;fork[15]=50;
  fork[259]=29;uint8_t *data=fork+260;data[1]=129;
  memset(data+10,255,4);data[14]=4;memcpy(data+15,"Game",4);
  data[19]=4;memcpy(data+20,"Quit",4);data[25]='Q';
  fork[537]=28;fork[539]=50;memcpy(fork+542,"MENU",4);fork[549]=10;
  fork[551]=129;fork[552]=255;fork[553]=255;
  if(kind==2)data[19]=255;
  if(kind==3)data[7]=1;
  MacRuntime r=fresh();CHECK(!mac_resource_open(&r.fork,fork,sizeof fork,NULL,0));
  uint32_t existing=0;if(kind==1){existing=mac_runtime_resource(&r,0x4d454e55,129);CHECK(existing);mac_cpu_ram(r.cpu)[rd(&r,existing,4)+15]='X';}
  if(kind==5 || kind==6){
   existing=mac_runtime_resource(&r,0x4d454e55,129);CHECK(existing);
   /* Model a purged resource master while retaining resource ownership. */
   wr(&r,existing,4,0);
   CHECK(!rd(&r,existing,4));if(kind==6)r.heap_limit=r.heap_next;
  }
  uint8_t low[0x200];memcpy(low,mac_cpu_ram(r.cpu),sizeof low);
  uint32_t heap=r.heap_next;wr(&r,SP,2,kind==4?999:129);int rc=call(&r,0xa9bf);
  if(kind==2 || kind==3){CHECK(rc==-1&&mac_cpu_halted(r.cpu)&&r.heap_next==heap);}
  else if(kind==6){CHECK(rc==1&&!rd(&r,SP+2,4)&&!rd(&r,existing,4)&&r.heap_next==heap);CHECK((int16_t)rd(&r,0xa60,2)==-108);}
  else if(kind==4){CHECK(rc==1&&!rd(&r,SP+2,4)&&(int16_t)rd(&r,0xa60,2)==-192);}
  else{
   CHECK(rc==1);uint32_t h=rd(&r,SP+2,4);unsigned count;MacHandle*m=mac_menu_record(&r,h,&count);
   CHECK(m&&count==1&&m->resource==2&&rd(&r,m->pointer+2,2)==64&&rd(&r,m->pointer+4,2)==16);
   CHECK(rd(&r,m->pointer+15,1)==(kind==1?'X':'G'));if(existing)CHECK(h==existing);
   wr(&r,SP,2,0);wr(&r,SP+2,4,h);CHECK(call(&r,0xa935)==1&&call(&r,0xa937)==1);
  }
  CHECK(!memcmp(low,mac_cpu_ram(r.cpu),sizeof low));
  mac_cpu_destroy(r.cpu);
 }
}
static void locking_and_capacity(void){
 MacRuntime r=fresh();uint32_t h=create(&r,128,"File");MacHandle*m=mac_menu_record(&r,h,NULL);
 mac_cpu_set(r.cpu,MAC_A0,h);CHECK(mac_memory_service(&r,0xa029)==1 && (m->state&0x80));
 uint32_t pointer=m->pointer,heap=r.heap_next;unsigned size=m->size;
 str(&r,"Quit");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,h);CHECK(call(&r,0xa933)==1);
 CHECK((int16_t)rd(&r,0x220,2)==-117&&m->pointer==pointer&&m->size==size&&r.heap_next==heap);
 unsigned n;MacHandle*l=mac_menu_list(&r,&n);CHECK(l&&n==0);
 mac_cpu_set(r.cpu,MAC_A0,l->handle);CHECK(mac_memory_service(&r,0xa029)==1);
 pointer=l->pointer;wr(&r,SP,2,0);wr(&r,SP+2,4,h);CHECK(call(&r,0xa935)==1);
 CHECK((int16_t)rd(&r,0x220,2)==-117&&l->pointer==pointer&&l->size==2&&r.heap_next==heap);
 mac_cpu_set(r.cpu,MAC_A0,l->handle);CHECK(mac_memory_service(&r,0xa02a)==1);
 for(unsigned i=0;i<32;i++){uint32_t next=i?create(&r,128+i,"M"):h;wr(&r,SP,2,0);wr(&r,SP+2,4,next);CHECK(call(&r,0xa935)==1);}
 CHECK(mac_menu_list(&r,&n)&&n==32);heap=r.heap_next;
 wr(&r,SP,2,0);wr(&r,SP+2,4,h);CHECK(call(&r,0xa935)==1&&r.heap_next==heap);
 uint32_t extra=create(&r,999,"Extra");pointer=l->pointer;heap=r.heap_next;
 wr(&r,SP,2,0);wr(&r,SP+2,4,extra);CHECK(call(&r,0xa935)==-1&&mac_cpu_halted(r.cpu));
 CHECK(l->pointer==pointer&&l->size==130&&r.heap_next==heap);mac_cpu_destroy(r.cpu);
}
int main(void){
 MacRuntime r=fresh();uint32_t h=create(&r,128,"File"),h2=create(&r,129,"Edit");CHECK(h&&h2);
 unsigned n;MacHandle*m=mac_menu_record(&r,h,&n);CHECK(m&&n==0&&m->size==20);CHECK(rd(&r,m->pointer,2)==128&&rd(&r,m->pointer+6,4)==0&&rd(&r,m->pointer+10,4)==UINT32_MAX);
 str(&r,"Open/O;(-;Quit/Q;Bold<B");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,h);CHECK(call(&r,0xa933)==1);
 CHECK(mac_menu_record(&r,h,&n)&&n==4);CHECK(!(rd(&r,m->pointer+10,4)&4));
 wr(&r,SP,4,0x44525652);wr(&r,SP+4,4,h);CHECK(call(&r,0xa94d)==1&&mac_menu_record(&r,h,&n)&&n==4);
 CHECK(rd(&r,m->pointer+19,1)==4 && rd(&r,m->pointer+25,1)=='O');
 wr(&r,SP,2,0x01ff);wr(&r,SP+2,2,1);wr(&r,SP+4,4,h);CHECK(call(&r,0xa945)==1&&rd(&r,m->pointer+26,1)==0x12);
 wr(&r,SP,2,0x00ff);CHECK(call(&r,0xa945)==1&&rd(&r,m->pointer+26,1)==0);
 wr(&r,SP,2,1);wr(&r,SP+2,4,h);CHECK(call(&r,0xa93a)==1&&!(rd(&r,m->pointer+10,4)&2));
 CHECK(call(&r,0xa939)==1&&(rd(&r,m->pointer+10,4)&2));
 wr(&r,SP,4,h);CHECK(call(&r,0xa950)==1&&rd(&r,SP+4,2)==4);
 wr(&r,SP,2,0);wr(&r,SP+2,4,h2);CHECK(call(&r,0xa935)==1);
 wr(&r,SP,2,129);wr(&r,SP+2,4,h);CHECK(call(&r,0xa935)==1);
 MacHandle*l=mac_menu_list(&r,&n);CHECK(l&&n==2&&rd(&r,l->pointer+2,4)==h&&rd(&r,l->pointer+6,4)==h2);
 CHECK(call(&r,0xa935)==1);CHECK(mac_menu_list(&r,&n)&&n==2);
 CHECK(call(&r,0xa937)==1);unsigned dark=0;for(unsigned y=5;y<13;y++)for(unsigned x=16;x<48;x++)if(mac_cpu_ram(r.cpu)[r.screen_base+y*640+x]==0)dark++;CHECK(dark>0);
 wr(&r,SP,2,128);CHECK(call(&r,0xa949)==1&&rd(&r,SP+2,4)==h);
 wr(&r,SP,2,128);CHECK(call(&r,0xa936)==1&&mac_menu_list(&r,&n)&&n==1);
 wr(&r,SP,4,h);CHECK(call(&r,0xa932)==1&&!rd(&r,h,4)&&!mac_menu_record(&r,h,NULL));
 uint32_t pi=create(&r,130,"\024");wr(&r,SP,2,0);wr(&r,SP+2,4,pi);CHECK(call(&r,0xa935)==1&&call(&r,0xa937)==1);
 mac_cpu_destroy(r.cpu);
 for(unsigned k=0;k<4;k++){
  r=fresh();h=create(&r,128,"File");m=mac_menu_record(&r,h,NULL);uint32_t pointer=m->pointer,heap=r.heap_next;
  if(k==0){str(&r,"Bad/");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,h);CHECK(call(&r,0xa933)==-1);}
  if(k==1){str(&r,"No^A");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,h);CHECK(call(&r,0xa933)==-1);}
  if(k==2){wr(&r,h,4,MAC_RUNTIME_RAM-1);wr(&r,SP,4,h);CHECK(call(&r,0xa950)==-1);}
  if(k==3){r.heap_limit=r.heap_next;CHECK(create(&r,129,"No memory")==0);CHECK((int16_t)rd(&r,0x220,2)==-108);}
  CHECK(m->pointer==pointer&&r.heap_next==heap);mac_cpu_destroy(r.cpu);
 }
 /* A real authored named resource must not be mistaken for an empty list. */
 uint8_t fork[600]={0};
 fork[2]=1;fork[6]=2;fork[11]=4;fork[15]=55;
 fork[512+25]=28;fork[512+27]=50;
 memcpy(fork+542,"DRVR",4);fork[549]=10;
 fork[551]=7;fork[562]=4;memcpy(fork+563,"Desk",4);
 r=fresh();CHECK(!mac_resource_open(&r.fork,fork,sizeof fork,NULL,0));h=create(&r,128,"System");
 wr(&r,SP,4,0x44525652);wr(&r,SP+4,4,h);
 CHECK(call(&r,0xa94d)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 locking_and_capacity();resources();puts("Menu records and titles: ok");return 0;
}
