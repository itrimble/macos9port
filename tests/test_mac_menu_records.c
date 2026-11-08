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
/* A MENU resource handle straight from GetResource: counting and reading items work (a real Mac lets it serve as a MenuHandle); changing it does not. */
static void raw_resource(void){
 uint8_t fork[600]={0};fork[2]=1;fork[6]=2;fork[11]=33;fork[15]=50;
 fork[259]=29;uint8_t *data=fork+260;data[1]=129;memset(data+10,255,4);data[14]=4;memcpy(data+15,"Game",4);data[19]=4;memcpy(data+20,"Quit",4);data[25]='Q';
 fork[537]=28;fork[539]=50;memcpy(fork+542,"MENU",4);fork[549]=10;fork[551]=129;fork[552]=255;fork[553]=255;
 MacRuntime r=fresh();CHECK(!mac_resource_open(&r.fork,fork,sizeof fork,NULL,0));
 uint32_t raw=mac_runtime_resource(&r,0x4d454e55,129);CHECK(raw);
 wr(&r,SP,4,raw);CHECK(call(&r,0xa950)==1&&rd(&r,SP+4,2)==1);
 wr(&r,SP+6,4,raw);wr(&r,SP+4,2,1);wr(&r,SP,4,0x4800);CHECK(call(&r,0xa946)==1&&rd(&r,0x4800,1)==4&&!memcmp(mac_cpu_ram(r.cpu)+0x4801,"Quit",4));
 /* InsertMenu(raw, -1) adopts it: published sizes, an ordinary owned menu afterwards (AppendMenu works, and it is in the hierarchical list, not the bar). */
 {unsigned n0=0;MacHandle*l0=mac_menu_list(&r,&n0);(void)l0;
  CHECK(!mac_menu_record(&r,raw,NULL));                                                                      /* strict paths still refuse it before */
  wr(&r,SP,2,0xffff);wr(&r,SP+2,4,raw);CHECK(call(&r,0xa935)==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+6);
  MacHandle*m=mac_menu_record(&r,raw,NULL);CHECK(m&&m->resource==2&&rd(&r,m->pointer+2,2)==4*8+32&&rd(&r,m->pointer+4,2)==16&&r.hier_count==1&&r.hier_menus[0]==raw);
  wr(&r,SP,2,0xffff);wr(&r,SP+2,4,raw);CHECK(call(&r,0xa935)==1&&r.hier_count==1);                            /* a second InsertMenu adds nothing */
  str(&r,"More");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,raw);CHECK(call(&r,0xa933)==1);                              /* AppendMenu now works */
  wr(&r,SP,4,raw);CHECK(call(&r,0xa950)==1&&rd(&r,SP+4,2)==2);}
 mac_cpu_destroy(r.cpu);
}
int main(void){
 raw_resource();
 MacRuntime r=fresh();uint32_t h=create(&r,128,"File"),h2=create(&r,129,"Edit");CHECK(h&&h2);
 unsigned n;MacHandle*m=mac_menu_record(&r,h,&n);CHECK(m&&n==0&&m->size==20);CHECK(rd(&r,m->pointer,2)==128&&rd(&r,m->pointer+6,4)==0&&rd(&r,m->pointer+10,4)==UINT32_MAX);
 str(&r,"Open/O;(-;Quit/Q;Bold<B");wr(&r,SP,4,0x4000);wr(&r,SP+4,4,h);CHECK(call(&r,0xa933)==1);
 CHECK(mac_menu_record(&r,h,&n)&&n==4);CHECK(!(rd(&r,m->pointer+10,4)&4));
 wr(&r,SP,4,0x44525652);wr(&r,SP+4,4,h);CHECK(call(&r,0xa94d)==1&&mac_menu_record(&r,h,&n)&&n==4);
 CHECK(rd(&r,m->pointer+19,1)==4 && rd(&r,m->pointer+25,1)=='O');
 wr(&r,SP,2,0x01ff);wr(&r,SP+2,2,1);wr(&r,SP+4,4,h);CHECK(call(&r,0xa945)==1&&rd(&r,m->pointer+26,1)==0x12);
 wr(&r,SP,2,0x00ff);CHECK(call(&r,0xa945)==1&&rd(&r,m->pointer+26,1)==0);
 /* GetItem, GetItemCmd, GetItemMark, GetItemStyle, GetItemIcon: (menu, item, VAR out); an item outside the menu leaves the byte alone (empty string for GetItem). */
 #define GET(op,item,out) (wr(&r,SP+6,4,h),wr(&r,SP+4,2,(item)),wr(&r,SP,4,(out)),call(&r,(op))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+10)
 memset(mac_cpu_ram(r.cpu)+0x4800,0xee,16);
 CHECK(GET(0xa946,3,0x4800)&&rd(&r,0x4800,1)==4&&!memcmp(mac_cpu_ram(r.cpu)+0x4801,"Quit",4)&&rd(&r,0x4805,1)==0xee);
 CHECK(GET(0xa946,1,0x4800)&&rd(&r,0x4800,1)==4&&!memcmp(mac_cpu_ram(r.cpu)+0x4801,"Open",4));
 CHECK(GET(0xa946,9,0x4800)&&rd(&r,0x4800,1)==0&&GET(0xa946,0,0x4800)&&rd(&r,0x4800,1)==0);
 wr(&r,0x4800,1,0xee);CHECK(GET(0xa84e,3,0x4800)&&rd(&r,0x4800,1)=='Q'&&GET(0xa84e,1,0x4800)&&rd(&r,0x4800,1)=='O');
 CHECK(GET(0xa84e,9,0x4800)&&rd(&r,0x4800,1)=='O');
 CHECK(GET(0xa943,1,0x4800)&&rd(&r,0x4800,1)==0&&GET(0xa941,4,0x4800)&&rd(&r,0x4800,1)==1&&GET(0xa941,3,0x4800)&&rd(&r,0x4800,1)==0);
 wr(&r,0x4800,2,0xeeee);CHECK(GET(0xa93f,1,0x4800)&&rd(&r,0x4800,2)==0);
 wr(&r,0x4800,1,0xee);CHECK(GET(0xa941,0,0x4800)&&rd(&r,0x4800,1)==0xee&&GET(0xa941,5,0x4800)&&rd(&r,0x4800,1)==0xee);
 #define SET(op,item,word) (wr(&r,SP+4,4,h),wr(&r,SP+2,2,(item)),wr(&r,SP,2,(word)),call(&r,(op))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+8)
 /* SetItemStyle / SetItemMark / SetItemCmd / SetItemIcon (menu, item, byte in the high half of the pushed word); items outside the menu are ignored. */
 CHECK(SET(0xa942,3,0x0600)&&GET(0xa941,3,0x4800)&&rd(&r,0x4800,1)==6&&rd(&r,m->pointer+42,1)==6);   /* item 3's style byte */
 CHECK(SET(0xa944,3,0x4100)&&GET(0xa943,3,0x4800)&&rd(&r,0x4800,1)=='A');
 CHECK(SET(0xa84f,3,0x5a00)&&GET(0xa84e,3,0x4800)&&rd(&r,0x4800,1)=='Z');
 CHECK(SET(0xa940,3,0x0900)&&(wr(&r,0x4800,2,0xeeee),GET(0xa93f,3,0x4800))&&rd(&r,0x4800,2)==9&&SET(0xa940,3,0));   /* (an item icon makes CalcMenuSize refuse, so clear it again) */
 CHECK(GET(0xa946,3,0x4800)&&!memcmp(mac_cpu_ram(r.cpu)+0x4801,"Quit",4)&&GET(0xa941,1,0x4800)&&rd(&r,0x4800,1)==0&&GET(0xa941,4,0x4800)&&rd(&r,0x4800,1)==1);   /* neighbours and the text are untouched */
 {uint8_t*mem=mac_cpu_ram(r.cpu)+m->pointer;unsigned guard=0;for(unsigned k=0;k<5;k++){mem[m->size+k]=0x77;guard|=1u<<k;}(void)guard;
  CHECK(SET(0xa942,0,0x0700)&&SET(0xa942,5,0x0700)&&SET(0xa942,-1,0x0700)&&SET(0xa944,5,0x4200)&&SET(0xa84f,5,0x4200)&&SET(0xa940,5,0x4200)&&GET(0xa941,3,0x4800)&&rd(&r,0x4800,1)==6&&GET(0xa941,1,0x4800)&&rd(&r,0x4800,1)==0);
  for(unsigned k=0;k<5;k++)CHECK(mem[m->size+k]==0x77);}   /* out-of-range items change nothing, not even the bytes after the block */
 {MacRuntime q=fresh();wr(&q,SP+4,4,0x4800);wr(&q,SP+2,2,1);wr(&q,SP,2,0x0100);CHECK(call(&q,0xa942)==-1&&mac_cpu_halted(q.cpu));mac_cpu_destroy(q.cpu);}   /* not a menu */
 /* CalcMenuSize publishes the 8x8-text size: width = longest text*8+32, height = items*16. */
 wr(&r,SP,4,h);CHECK(call(&r,0xa948)==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+4&&rd(&r,m->pointer+2,2)==4*8+32&&rd(&r,m->pointer+4,2)==64);
 {MacRuntime q=fresh();wr(&q,SP,4,0);CHECK(call(&q,0xa948)==-1&&mac_cpu_halted(q.cpu));mac_cpu_destroy(q.cpu);}   /* nil menu */
 /* GetMCEntry: there are no menu colour tables, so never an entry. */
 wr(&r,SP,2,3);wr(&r,SP+2,2,128);wr(&r,SP+4,4,0x1234);CHECK(call(&r,0xaa64)==1&&rd(&r,SP+4,4)==0&&mac_cpu_get(r.cpu,MAC_A7)==SP+4);
 {MacRuntime q=fresh();uint32_t qh=create(&q,128,"File");wr(&q,SP+6,4,qh);wr(&q,SP+4,2,1);wr(&q,SP,4,0);CHECK(call(&q,0xa946)==-1&&mac_cpu_halted(q.cpu));mac_cpu_destroy(q.cpu);}   /* nil destination */
 {MacRuntime q=fresh();wr(&q,SP+6,4,0x4800);wr(&q,SP+4,2,1);wr(&q,SP,4,0x4900);CHECK(call(&q,0xa941)==-1&&mac_cpu_halted(q.cpu));mac_cpu_destroy(q.cpu);}       /* not a menu */
 {MacRuntime q=fresh();uint32_t qh=create(&q,128,"File");wr(&q,SP,4,qh);CHECK(call(&q,0xa948)==1);wr(&q,SP+6,4,qh);wr(&q,SP+4,2,1);wr(&q,SP,4,(uint32_t)MAC_RUNTIME_RAM-1);CHECK(call(&q,0xa93f)==-1&&mac_cpu_halted(q.cpu));mac_cpu_destroy(q.cpu);}   /* odd/out-of-RAM destination */
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
