/* SetItem ($A947): literal text replacement that keeps every other item attribute. */
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
static uint32_t menu(MacRuntime*r,const char*items){
 str(r,"File");wr(r,SP,4,0x4000);wr(r,SP+4,2,128);CHECK(call(r,0xa931)==1);uint32_t h=rd(r,SP+6,4);
 str(r,items);wr(r,SP,4,0x4000);wr(r,SP+4,4,h);CHECK(call(r,0xa933)==1);return h;
}
static int set_item(MacRuntime*r,uint32_t h,unsigned item,const char*s){
 str(r,s);wr(r,SP,4,0x4000);wr(r,SP+4,2,item);wr(r,SP+6,4,h);return call(r,0xa947);
}
/* Fields of item n (1-based): returns the offset of its length byte. */
static uint32_t at(MacRuntime*r,uint32_t h,unsigned n){
 uint32_t p=rd(r,h,4)+15+rd(r,rd(r,h,4)+14,1);
 for(unsigned i=1;i<n;i++)p+=rd(r,p,1)+5;return p;
}
static int is(MacRuntime*r,uint32_t p,const char*s){size_t n=strlen(s);return rd(r,p,1)==n && !memcmp(mac_cpu_ram(r->cpu)+p+1,s,n);}
int main(void){
 MacRuntime r=fresh();uint32_t h=menu(&r,"Open/O;(Quit/Q;Bold<B");unsigned n;MacHandle*m=mac_menu_record(&r,h,&n);CHECK(m&&n==3);
 uint32_t flags=rd(&r,m->pointer+10,4);
 /* Grow the middle item: text changes, key/enable kept, neighbours intact, header width and size follow. */
 CHECK(set_item(&r,h,2,"Exit The Game")==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
 m=mac_menu_record(&r,h,&n);CHECK(m && n==3);
 CHECK(is(&r,at(&r,h,1),"Open") && is(&r,at(&r,h,2),"Exit The Game") && is(&r,at(&r,h,3),"Bold"));
 uint32_t q=at(&r,h,2);CHECK(rd(&r,q+1+13+1,1)=='Q' && rd(&r,q+1+13+3,1)==0);
 CHECK(rd(&r,at(&r,h,3)+1+4+3,1)==1); /* Bold style bit survives */
 CHECK(rd(&r,m->pointer+10,4)==flags && !(flags&4)); /* item 2 stays disabled */
 CHECK(rd(&r,m->pointer+2,2)==13*8+32 && rd(&r,m->pointer+4,2)==48);
 /* Shrink it again: the width falls back. */
 CHECK(set_item(&r,h,2,"X")==1 && is(&r,at(&r,h,2),"X") && is(&r,at(&r,h,3),"Bold"));
 CHECK(rd(&r,m->pointer+2,2)==4*8+32);
 /* Text is literal: metacharacters are not interpreted and no attribute changes. */
 CHECK(set_item(&r,h,1,"(Hi/A;!x")==1 && is(&r,at(&r,h,1),"(Hi/A;!x"));
 CHECK(rd(&r,at(&r,h,1)+1+8+1,1)=='O' && rd(&r,at(&r,h,1)+1+8+2,1)==0 && mac_menu_record(&r,h,&n) && n==3);
 /* Out-of-range items are ignored but still pop their arguments. */
 uint8_t before[64];memcpy(before,mac_cpu_ram(r.cpu)+m->pointer,sizeof before);
 CHECK(set_item(&r,h,0,"Zero")==1 && set_item(&r,h,4,"Four")==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+10);
 CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+m->pointer,sizeof before));
 /* Failures halt with a diagnostic and leave the record alone. */
 uint32_t pointer=m->pointer;unsigned size=m->size;
 CHECK(set_item(&r,h,1,"")==-1 && mac_cpu_halted(r.cpu) && m->pointer==pointer && m->size==size);
 mac_cpu_destroy(r.cpu);
 r=fresh();h=menu(&r,"Open;Quit");CHECK(set_item(&r,0x1234,1,"Bad")==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
 /* A locked menu cannot grow: -117, content unchanged. */
 r=fresh();h=menu(&r,"Open;Quit");m=mac_menu_record(&r,h,NULL);mac_cpu_set(r.cpu,MAC_A0,h);CHECK(mac_memory_service(&r,0xa029)==1 && (m->state&0x80));
 pointer=m->pointer;size=m->size;CHECK(set_item(&r,h,1,"Something much longer")==1);
 CHECK((int16_t)rd(&r,0x220,2)==-117 && m->pointer==pointer && m->size==size && is(&r,at(&r,h,1),"Open"));
 mac_cpu_destroy(r.cpu);
 puts("SetItem checks passed");return 0;
}
