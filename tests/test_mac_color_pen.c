#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int mac_color_pen_service(MacRuntime*,uint16_t);
#define CHECK(c) do { if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);} } while(0)
#define SP 0x8000u
#define PORT 0x9000u
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,p,n,v));}
static MacRuntime fresh(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    r.heap_next=0x10000;r.heap_limit=0x100000;r.qd_globals=0x7000;
    wr(&r,SP,4,PORT);mac_cpu_set(r.cpu,MAC_A7,SP);
    CHECK(mac_color_port_service(&r,0xaa00)==1);return r;
}
static int call(MacRuntime*r,uint16_t op){mac_cpu_set(r->cpu,MAC_A7,SP);return mac_color_pen_service(r,op);}
static void patterns(void){
    MacRuntime r=fresh();uint32_t pen=rd(&r,rd(&r,PORT+58,4),4),back=rd(&r,rd(&r,PORT+32,4),4);
    uint8_t bits[8]={0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55};
    memcpy(mac_cpu_ram(r.cpu)+0xa001,bits,8);wr(&r,SP,4,0xa001);
    CHECK(call(&r,0xa89d)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    CHECK(!memcmp(mac_cpu_ram(r.cpu)+pen+20,bits,8));
    CHECK(call(&r,0xa87c)==1 && !memcmp(mac_cpu_ram(r.cpu)+back+20,bits,8));
    /* Genuine indexed rendering consumes the changed pen and background. */
    wr(&r,0xb000,4,0);wr(&r,0xb004,2,2);wr(&r,0xb006,2,8);wr(&r,SP,4,0xb000);
    mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_color_raster_service(&r,0xa8a2)==1);
    uint8_t *screen=mac_cpu_ram(r.cpu)+r.screen_base;
    CHECK(screen[0]==255 && screen[1]==0 && screen[640]==0 && screen[641]==255);
    memset(screen,17,8);mac_cpu_set(r.cpu,MAC_A7,SP);CHECK(mac_color_raster_service(&r,0xa8a3)==1);
    CHECK(screen[0]==255 && screen[1]==0);
    /* Overlapping source is snapshotted before mutation. */
    memcpy(bits,mac_cpu_ram(r.cpu)+pen+19,8);wr(&r,SP,4,pen+19);
    CHECK(call(&r,0xa89d)==1 && !memcmp(mac_cpu_ram(r.cpu)+pen+20,bits,8));
    mac_cpu_destroy(r.cpu);
}
static void state(void){
    MacRuntime r=fresh();wr(&r,SP,2,3);wr(&r,SP+2,2,5);CHECK(call(&r,0xa89b)==1);
    CHECK(rd(&r,PORT+52,4)==0x00030005);
    wr(&r,SP,2,0xffff);wr(&r,SP+2,2,7);CHECK(call(&r,0xa893)==1);
    wr(&r,SP,4,0xb000);CHECK(call(&r,0xa89a)==1 && rd(&r,0xb000,4)==0xffff0007);
    CHECK(call(&r,0xa896)==1 && call(&r,0xa896)==1 && rd(&r,PORT+66,2)==0xfffe);
    CHECK(call(&r,0xa897)==1 && rd(&r,PORT+66,2)==0xffff);
    wr(&r,SP,2,15);CHECK(call(&r,0xa89c)==1 && rd(&r,PORT+56,2)==15);
    wr(&r,SP,2,44);CHECK(call(&r,0xa89c)==1 && rd(&r,PORT+56,2)==44);                                      /* transparent + patCopy */
    CHECK(call(&r,0xa89e)==1 && rd(&r,PORT+52,4)==0x00010001 && rd(&r,PORT+56,2)==8);
    CHECK(rd(&r,PORT+48,4)==0xffff0007 && rd(&r,PORT+66,2)==0xffff);
    uint32_t pen=rd(&r,rd(&r,PORT+58,4),4);CHECK(rd(&r,pen+20,4)==0xffffffff);
    CHECK(call(&r,0xabcd)==0);wr(&r,PORT+6,2,80);CHECK(call(&r,0xa89d)==0);
    mac_cpu_destroy(r.cpu);
}
static void failures(void){
    for(unsigned kind=0;kind<8;kind++){
        MacRuntime r=fresh();uint32_t h=rd(&r,PORT+58,4),p=rd(&r,h,4);uint16_t op=0xa89d;
        wr(&r,SP,4,0xa000);memset(mac_cpu_ram(r.cpu)+0xa000,0x55,8);
        if(kind==0)wr(&r,h,4,p+2);
        if(kind==1)wr(&r,p,2,1);
        if(kind==2) { for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==h)r.handles[i].resource=1; }
        if(kind==3)wr(&r,SP,4,MAC_RUNTIME_RAM-7);
        if(kind==4){op=0xa89c;wr(&r,SP,2,16);}
        if(kind==5){op=0xa89b;wr(&r,SP,2,0xffff);}
        if(kind==6){op=0xa89e;wr(&r,p,2,1);wr(&r,PORT+52,4,0x00030004);}
        if(kind==7){op=0xa897;wr(&r,PORT+66,2,32767);}
        uint8_t before[108],pattern[28];memcpy(before,mac_cpu_ram(r.cpu)+PORT,108);memcpy(pattern,mac_cpu_ram(r.cpu)+p,28);
        CHECK(call(&r,op)==-1 && mac_cpu_halted(r.cpu) && mac_cpu_get(r.cpu,MAC_A7)==SP);
        if(kind==4)CHECK(strstr(mac_cpu_error(r.cpu),"mode 16 ")!=NULL);                                      /* the refusal names the mode */
        CHECK(!memcmp(before,mac_cpu_ram(r.cpu)+PORT,108) && !memcmp(pattern,mac_cpu_ram(r.cpu)+p,28));
        mac_cpu_destroy(r.cpu);
    }
    MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,MAC_RUNTIME_RAM-2);
    CHECK(mac_color_pen_service(&r,0xa89d)==-1);mac_cpu_destroy(r.cpu);
}

/* LineTo ($A891), Line ($A892) and Move ($A894) on a colour port: an axis-aligned line paints the rectangle the pen sweeps, in the pen
 * pattern and mode, clipped; Move and a hidden pen only move; a diagonal halts by name. v is on top of the stack. */
static unsigned scr(MacRuntime*r,int x,int y){return mac_cpu_ram(r->cpu)[r->screen_base+(unsigned)y*640u+(unsigned)x];}
static int pen_call(MacRuntime*r,uint16_t op,int h,int v){wr(r,SP,2,(uint16_t)v);wr(r,SP+2,2,(uint16_t)h);return call(r,op);}
static int pen_at(MacRuntime*r,int*v,int*h){*v=(int16_t)rd(r,PORT+48,2);*h=(int16_t)rd(r,PORT+50,2);return 1;}
static void lines(void){
    MacRuntime r=fresh();int v,h;memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);
    CHECK(pen_call(&r,0xa893,10,5)==1);                                                   /* MoveTo(10,5) */
    CHECK(pen_call(&r,0xa891,20,5)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);                /* LineTo(20,5): a 1x1 pen sweeps x 10..20 */
    pen_at(&r,&v,&h);CHECK(v==5 && h==20);
    for(int x=9;x<=21;x++)CHECK(scr(&r,x,5)==((x>=10&&x<=20)?255u:17u));
    CHECK(scr(&r,15,4)==17 && scr(&r,15,6)==17);
    CHECK(pen_call(&r,0xa891,20,9)==1);for(int y=4;y<=10;y++)CHECK(scr(&r,20,y)==((y>=5&&y<=9)?255u:17u));      /* vertical */
    CHECK(pen_call(&r,0xa892,-5,0)==1);pen_at(&r,&v,&h);CHECK(v==9 && h==15);                                   /* Line(-5,0): leftwards */
    for(int x=14;x<=21;x++)CHECK(scr(&r,x,9)==((x>=15&&x<=20)?255u:17u));
    CHECK(pen_call(&r,0xa892,0,-3)==1);pen_at(&r,&v,&h);CHECK(v==6 && h==15);for(int y=4;y<=10;y++)CHECK(scr(&r,15,y)==((y>=5&&y<=9)?255u:17u));   /* upwards: rows 6 to 9, joining the first line at row 5 */
    CHECK(pen_call(&r,0xa894,30,12)==1);pen_at(&r,&v,&h);CHECK(v==18 && h==45 && scr(&r,40,15)==17 && scr(&r,45,18)==17);   /* Move(dh,dv): relative, no ink */
    mac_cpu_destroy(r.cpu);
    /* a zero-length LineTo is a dot; a bigger pen sweeps a bigger rectangle (width 2, height 3) */
    r=fresh();memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);
    pen_call(&r,0xa893,4,4);CHECK(pen_call(&r,0xa891,4,4)==1 && scr(&r,4,4)==255 && scr(&r,5,4)==17 && scr(&r,4,5)==17);
    wr(&r,SP,2,3);wr(&r,SP+2,2,2);CHECK(call(&r,0xa89b)==1);                                      /* PenSize(2 wide, 3 high) */
    pen_call(&r,0xa893,10,10);pen_call(&r,0xa891,20,10);
    for(int y=9;y<=13;y++)for(int x=9;x<=22;x++)CHECK(scr(&r,x,y)==((y>=10&&y<=12&&x>=10&&x<=21)?255u:17u));
    mac_cpu_destroy(r.cpu);
    /* the pen pattern and mode decide the pixels: 0xAA rows, patCopy paints the background where the pattern is clear */
    r=fresh();memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);
    {uint8_t bits[8]={0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa};memcpy(mac_cpu_ram(r.cpu)+0xa001,bits,8);wr(&r,SP,4,0xa001);CHECK(call(&r,0xa89d)==1);}
    pen_call(&r,0xa893,10,5);pen_call(&r,0xa891,13,5);CHECK(scr(&r,10,5)==255 && scr(&r,11,5)==0 && scr(&r,12,5)==255 && scr(&r,13,5)==0);
    wr(&r,SP,2,9);CHECK(call(&r,0xa89c)==1);                                                       /* PenMode(patOr = 9): ink only where the pattern is set */
    memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);pen_call(&r,0xa891,10,5);pen_call(&r,0xa893,10,6);pen_call(&r,0xa891,13,6);
    CHECK(scr(&r,10,6)==255 && scr(&r,11,6)==17 && scr(&r,12,6)==255 && scr(&r,13,6)==17);
    mac_cpu_destroy(r.cpu);
    /* a hidden pen moves without painting; showing it again paints */
    r=fresh();memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);
    CHECK(call(&r,0xa896)==1);pen_call(&r,0xa893,5,5);CHECK(pen_call(&r,0xa891,15,5)==1);pen_at(&r,&v,&h);CHECK(v==5 && h==15 && scr(&r,8,5)==17);
    CHECK(call(&r,0xa897)==1);pen_call(&r,0xa891,25,5);CHECK(scr(&r,20,5)==255 && scr(&r,14,5)==17);
    mac_cpu_destroy(r.cpu);
    /* a diagonal is a named halt that leaves the pen and the pixels alone */
    r=fresh();memset(mac_cpu_ram(r.cpu)+r.screen_base,17,640*40);pen_call(&r,0xa893,5,5);
    CHECK(pen_call(&r,0xa891,9,9)==-1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"Diagonal") && scr(&r,7,7)==17);mac_cpu_destroy(r.cpu);
    r=fresh();pen_call(&r,0xa893,5,5);CHECK(pen_call(&r,0xa892,3,-3)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    /* a diagonal Move is fine (it draws nothing) */
    r=fresh();pen_call(&r,0xa893,5,5);CHECK(pen_call(&r,0xa894,3,-3)==1);pen_at(&r,&v,&h);CHECK(v==2 && h==8);mac_cpu_destroy(r.cpu);
    /* a transfer mode the colour raster does not do is refused rather than drawn wrongly */
    r=fresh();wr(&r,PORT+56,2,40);pen_call(&r,0xa893,5,5);CHECK(pen_call(&r,0xa891,9,5)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);      /* an arithmetic mode */
}
int main(void){lines();patterns();state();failures();puts("mac color pen tests passed");return 0;}
