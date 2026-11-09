/* DLOG / DITL parsers: layout, bounds and refusal of malformed or truncated resources. */
#include "mac_ditl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct{uint8_t b[1024];size_t n;} W;
static void byte(W*w,unsigned v){CHECK(w->n<sizeof w->b);w->b[w->n++]=(uint8_t)v;}
static void word(W*w,unsigned v){byte(w,v>>8);byte(w,v);}
/* One DITL item: rect, type byte, then data (a string, or a 2-byte ID), padded to even. */
static void item(W*w,int t,int l,int b,int r,unsigned type,const void*data,unsigned len){
 word(w,0);word(w,0);word(w,t);word(w,l);word(w,b);word(w,r);byte(w,type);byte(w,len);
 for(unsigned i=0;i<len;i++)byte(w,((const uint8_t*)data)[i]);if(len&1)byte(w,0);
}
static void sample(W*w){
 memset(w,0,sizeof *w);word(w,3); /* four items */
 item(w,138,256,156,336,4,"Open",4);
 item(w,39,12,185,230,0,"",0);
 item(w,189,12,206,247,0x88,"Pick one",8);
 item(w,3,312,35,344,0xa0,"\x03\xe5",2);
}
static void dlog(W*w,unsigned title_len,int trailing){
 memset(w,0,sizeof *w);word(w,10);word(w,20);word(w,218);word(w,368);word(w,1);byte(w,1);byte(w,0);byte(w,0);byte(w,0);
 word(w,0x1234);word(w,0x5678);word(w,256);byte(w,title_len);for(unsigned i=0;i<title_len;i++)byte(w,'a'+i);
 if(trailing&&(w->n&1))byte(w,0);for(int i=0;i<trailing;i++)byte(w,0);
}
int main(void){
 W w;MacDitl d;const char*e=NULL;
 sample(&w);CHECK(!mac_ditl_parse(w.b,w.n,&d,&e)&&d.count==4);
 CHECK(d.items[0].kind==MAC_ITEM_BUTTON&&!d.items[0].disabled&&d.items[0].length==4&&!memcmp(d.items[0].data,"Open",4));
 CHECK(d.items[0].top==138&&d.items[0].left==256&&d.items[0].bottom==156&&d.items[0].right==336);
 CHECK(d.items[1].kind==MAC_ITEM_USER&&d.items[1].length==0&&d.items[1].right==230);
 CHECK(d.items[2].kind==MAC_ITEM_STATIC&&d.items[2].disabled&&d.items[2].length==8);
 CHECK(d.items[3].kind==MAC_ITEM_ICON&&d.items[3].disabled&&d.items[3].data[0]==3&&d.items[3].data[1]==0xe5);
 /* Odd text length: the pad byte is consumed, the next item still parses. */
 memset(&w,0,sizeof w);word(&w,1);item(&w,0,0,10,10,8,"abc",3);item(&w,1,1,11,11,4,"ok",2);
 CHECK(!mac_ditl_parse(w.b,w.n,&d,&e)&&d.count==2&&d.items[1].top==1&&!memcmp(d.items[0].data,"abc",3));
 /* A Help Manager item (kind 1, 4 bytes) parses; any other length does not. */
 memset(&w,0,sizeof w);word(&w,1);item(&w,0,0,0,0,4,"OK",2);item(&w,0,0,0,0,1,"\0\1\0\2",4);
 CHECK(!mac_ditl_parse(w.b,w.n,&d,&e)&&d.count==2&&d.items[1].kind==MAC_ITEM_HELP&&d.items[1].length==4);
 memset(&w,0,sizeof w);word(&w,0);item(&w,0,0,0,0,1,"\0\1",2);CHECK(mac_ditl_parse(w.b,w.n,&d,&e)==-1);
 /* Every truncation fails, and a trailing byte fails. */
 sample(&w);for(size_t n=0;n<w.n;n++){CHECK(mac_ditl_parse(w.b,n,&d,&e)==-1&&e);}
 sample(&w);byte(&w,0);CHECK(mac_ditl_parse(w.b,w.n,&d,&e)==-1);
 /* Unknown kind, wrong resource-item length, too many items. */
 sample(&w);w.b[2+12]=9;CHECK(mac_ditl_parse(w.b,w.n,&d,&e)==-1); /* kind 9 */
 memset(&w,0,sizeof w);word(&w,0);item(&w,0,0,9,9,32,"abc",3);CHECK(mac_ditl_parse(w.b,w.n,&d,&e)==-1);
 memset(&w,0,sizeof w);word(&w,MAC_DITL_MAX_ITEMS);for(int i=0;i<=MAC_DITL_MAX_ITEMS;i++)item(&w,0,0,9,9,4,"",0);
 CHECK(mac_ditl_parse(w.b,w.n,&d,&e)==-1);
 memset(&w,0,sizeof w);word(&w,MAC_DITL_MAX_ITEMS-1);for(int i=0;i<MAC_DITL_MAX_ITEMS;i++)item(&w,0,0,9,9,4,"",0);
 CHECK(!mac_ditl_parse(w.b,w.n,&d,&e)&&d.count==MAC_DITL_MAX_ITEMS);
 /* DLOG */
 MacDlog g;
 dlog(&w,0,0);CHECK(w.n==21&&!mac_dlog_parse(w.b,w.n,&g,&e));
 CHECK(g.top==10&&g.left==20&&g.bottom==218&&g.right==368&&g.proc_id==1&&g.visible==1&&g.go_away==0);
 CHECK(g.ref_con==0x12345678&&g.items_id==256&&g.title_length==0);
 dlog(&w,3,0);CHECK(!mac_dlog_parse(w.b,w.n,&g,&e)&&g.title_length==3&&!memcmp(g.title,"abc",3)); /* padded to even */
 dlog(&w,3,2);CHECK(!mac_dlog_parse(w.b,w.n,&g,&e)); /* optional position word */
 dlog(&w,0,2);CHECK(!mac_dlog_parse(w.b,w.n,&g,&e));
 dlog(&w,0,1);CHECK(mac_dlog_parse(w.b,w.n,&g,&e)==-1); /* stray byte */
 dlog(&w,3,3);CHECK(mac_dlog_parse(w.b,w.n,&g,&e)==-1);
 dlog(&w,4,0);for(size_t n=0;n<21+4;n++){CHECK(mac_dlog_parse(w.b,n,&g,&e)==-1);}
 dlog(&w,0,0);memcpy(w.b+6,w.b+2,2);CHECK(mac_dlog_parse(w.b,w.n,&g,&e)==-1); /* right := left */
 dlog(&w,0,0);memcpy(w.b+4,w.b,2);CHECK(mac_dlog_parse(w.b,w.n,&g,&e)==-1); /* bottom := top */
 puts("DLOG/DITL parser checks passed");return 0;
}
