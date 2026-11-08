#include "mac_runtime.h"
#include <stdio.h>
#include <string.h>
int mac_color_raster_service(MacRuntime *,uint16_t);
int mac_shapes_service(MacRuntime *,uint16_t);
static int failures;
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
enum { PORT=0x100, PMH=0x200, PM=0x210, RH=0x250, R=0x260, PH=0x280, P=0x290, RECT=0x300, SP=0x400, PIX=0x800 };
static void w(MacRuntime *r,uint32_t a,unsigned n,uint32_t v){CHECK(!mac_cpu_write(r->cpu,a,n,v));}
static void rect(MacRuntime *r,uint32_t a,int t,int l,int b,int rr){w(r,a,2,t);w(r,a+2,2,l);w(r,a+4,2,b);w(r,a+6,2,rr);}
static MacRuntime setup(void){
    MacRuntime r={0};r.cpu=mac_cpu_create(8192);r.qd_globals=0x80;
    w(&r,0x80,4,PORT);w(&r,PORT+6,2,0xc000);w(&r,PORT+2,4,PMH);w(&r,PMH,4,PM);
    w(&r,PM,4,PIX);w(&r,PM+4,2,0x8008);rect(&r,PM+6,0,0,4,8);
    w(&r,PM+32,2,8);w(&r,PM+34,2,1);w(&r,PM+36,2,8);rect(&r,PORT+16,0,0,4,8);
    w(&r,PORT+24,4,RH);w(&r,PORT+28,4,RH);w(&r,RH,4,R);w(&r,R,2,10);rect(&r,R+2,0,0,4,8);
    w(&r,PORT+32,4,PH);w(&r,PORT+58,4,PH);w(&r,PH,4,P);memset(mac_cpu_ram(r.cpu)+P+20,0xaa,8);
    w(&r,PORT+80,4,3);w(&r,PORT+84,4,7);w(&r,PORT+52,2,1);w(&r,PORT+54,2,1);w(&r,PORT+56,2,8);
    rect(&r,RECT,0,0,4,8);return r;
}
static int call(MacRuntime *r,uint16_t op){mac_cpu_set(r->cpu,MAC_A7,SP);w(r,SP,4,RECT);return mac_color_raster_service(r,op);}
int main(void){
    MacRuntime r=setup();uint8_t *mem=mac_cpu_ram(r.cpu);
    const uint8_t expected[8][2]={{3,7},{3,9},{246,9},{7,9},{7,3},{9,3},{9,246},{9,7}};
    for(int mode=8;mode<16;mode++){
        memset(mem+PIX,9,32);w(&r,PORT+56,2,mode);CHECK(call(&r,0xa8a2)==1);
        CHECK(mem[PIX]==expected[mode-8][0] && mem[PIX+1]==expected[mode-8][1]);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    }
    w(&r,PORT+56,2,8);memset(mem+PIX,9,32);rect(&r,R+2,1,2,3,6);
    CHECK(call(&r,0xa8a2)==1);CHECK(mem[PIX]==9 && mem[PIX+8+2]==3 && mem[PIX+8+1]==9 && mem[PIX+8+6]==9);
    rect(&r,R+2,0,0,4,8);memset(mem+PIX,9,32);CHECK(call(&r,0xa8a1)==1);
    CHECK(mem[PIX+8+2]==9 && mem[PIX+2]==3 && mem[PIX+3*8+2]==3);
    CHECK(call(&r,0xa8a3)==1);CHECK(mem[PIX]==3 && mem[PIX+1]==7);
    CHECK(call(&r,0xa8a4)==1);CHECK(mem[PIX]==252 && mem[PIX+1]==248);
    mac_cpu_set(r.cpu,MAC_A7,SP);w(&r,SP,4,P+20);w(&r,SP+4,4,RECT);
    CHECK(mac_color_raster_service(&r,0xa8a5)==1);CHECK(mem[PIX]==3 && mem[PIX+1]==7);
    CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP+8);
    r.palette[19]=0xff0000;w(&r,RECT,2,65535);w(&r,RECT+2,2,0);w(&r,RECT+4,2,0);
    CHECK(call(&r,0xaa14)==1);CHECK(mac_cpu_read(r.cpu,PORT+80,4)==19);CHECK(mac_cpu_read(r.cpu,PORT+36,2)==65535);
    CHECK(call(&r,0xaa15)==1);CHECK(mac_cpu_read(r.cpu,PORT+84,4)==19);
    w(&r,PORT+6,2,8);CHECK(call(&r,0xa8a2)==0);CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);
    mac_cpu_destroy(r.cpu);
    for(int error=0;error<4;error++){
        r=setup();mem=mac_cpu_ram(r.cpu);memset(mem+PIX,99,32);
        if(error==0)w(&r,P,2,1);
        if(error==1)w(&r,R,2,12);
        if(error==2)w(&r,PORT+56,2,32);
        if(error==3)w(&r,PM,4,8190);
        CHECK(call(&r,0xa8a2)==-1);CHECK(mac_cpu_halted(r.cpu));
        for(int i=0;i<32;i++)CHECK(mem[PIX+i]==99);
        CHECK(mac_cpu_get(r.cpu,MAC_A7)==SP);mac_cpu_destroy(r.cpu);
    }
    /* a source mode as the pen mode does what the pattern mode of the same operation does (srcCopy 0 = patCopy 8, ... notSrcBic 7 = notPatBic 15) */
    for(int mode=0;mode<8;mode++){
        r=setup();mem=mac_cpu_ram(r.cpu);memset(mem+PIX,9,32);w(&r,PORT+56,2,mode);CHECK(call(&r,0xa8a2)==1);
        uint8_t a0=mem[PIX],a1=mem[PIX+1];memset(mem+PIX,9,32);w(&r,PORT+56,2,mode+8);CHECK(call(&r,0xa8a2)==1);CHECK(mem[PIX]==a0&&mem[PIX+1]==a1);mac_cpu_destroy(r.cpu);
    }
    /* PenMode(44) = transparent + patCopy: set pattern bits paint the foreground, clear bits leave the destination; fg == bg paints nothing */
    r=setup();mem=mac_cpu_ram(r.cpu);memset(mem+PIX,9,32);w(&r,PORT+56,2,44);rect(&r,RECT,0,0,4,8);rect(&r,R+2,0,0,4,8);
    CHECK(call(&r,0xa8a2)==1);for(int y=0;y<4;y++)for(int x=0;x<8;x++)CHECK(mem[PIX+y*8+x]==((x&1)?9:3));                 /* $AA: even columns are set */
    memset(mem+PIX,9,32);CHECK(call(&r,0xa8a1)==1);CHECK(mem[PIX]==3&&mem[PIX+1]==9&&mem[PIX+8+1]==9&&mem[PIX+8+2]==9);   /* a frame in transparent mode: only the edge's set bits */
    memset(mem+PIX,9,32);w(&r,PORT+84,4,3);CHECK(call(&r,0xa8a2)==1);for(int i=0;i<32;i++)CHECK(mem[PIX+i]==9);          /* background equals foreground (3): transparent */
    w(&r,PORT+84,4,7);memset(mem+PIX,9,32);w(&r,PORT+56,2,8);CHECK(call(&r,0xa8a2)==1);CHECK(mem[PIX]==3&&mem[PIX+1]==7);   /* patCopy still paints both */
    w(&r,PORT+56,2,44);memset(mem+PIX,9,32);mac_cpu_set(r.cpu,MAC_A7,SP);w(&r,SP,4,RECT);CHECK(mac_shapes_service(&r,0xa8b8)==1);CHECK(mem[PIX+2]==3&&mem[PIX+3]==9&&mem[PIX]==9);        /* an oval in transparent mode too */
    mac_cpu_destroy(r.cpu);
    /* Ovals and round rectangles on the 8 x 4 indexed port: pen pattern $AA (even columns foreground 3), background 7, field filled with 9. */
    r=setup();mem=mac_cpu_ram(r.cpu);
    #define SH(op,args) (mac_cpu_set(r.cpu,MAC_A7,SP),(args),mac_shapes_service(&r,(op)))
    #define ROW(y,lo,hi,even,odd) do{for(int x=0;x<8;x++){unsigned want=(x>=(lo)&&x<(hi))?((x&1)?(odd):(even)):9;if(mem[PIX+(y)*8+x]!=want){fprintf(stderr,"row %d col %d: %u want %u (line %d)\n",(y),x,mem[PIX+(y)*8+x],want,__LINE__);failures++;}}}while(0)
    memset(mem+PIX,9,32);CHECK(SH(0xa8b8,w(&r,SP,4,RECT))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+4);                      /* PaintOval */
    ROW(0,2,6,3,7);ROW(1,0,8,3,7);ROW(2,0,8,3,7);ROW(3,2,6,3,7);
    memset(mem+PIX,9,32);CHECK(SH(0xa8b7,w(&r,SP,4,RECT))==1);                                                         /* FrameOval, pen 1 x 1 */
    ROW(0,2,6,3,7);ROW(3,2,6,3,7);
    for(int y=1;y<3;y++)for(int x=0;x<8;x++){unsigned want=(x<2||x>=6)?((x&1)?7:3):9;CHECK(mem[PIX+y*8+x]==want);}
    memset(mem+PIX,9,32);CHECK(SH(0xa8b9,w(&r,SP,4,RECT))==1);ROW(0,2,6,3,7);ROW(1,0,8,3,7);                           /* EraseOval: background pattern (here the same $AA) */
    memset(mem+PIX,9,32);CHECK(SH(0xa8ba,w(&r,SP,4,RECT))==1);                                                         /* InvertOval */
    for(int y=0;y<4;y++)for(int x=0;x<8;x++){int in=(y==0||y==3)?(x>=2&&x<6):1;CHECK(mem[PIX+y*8+x]==(in?(9^255):9));}
    memset(mem+PIX,9,32);CHECK(SH(0xa8bb,(w(&r,SP,4,P+20),w(&r,SP+4,4,RECT)))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+8);ROW(0,2,6,3,7);   /* FillOval with a pattern pointer */
    w(&r,PORT+52,2,2);w(&r,PORT+54,2,2);memset(mem+PIX,9,32);CHECK(SH(0xa8b7,w(&r,SP,4,RECT))==1);                      /* a 2 x 2 pen: inner is 4 x 0, so the whole oval is the frame */
    ROW(0,2,6,3,7);ROW(1,0,8,3,7);ROW(2,0,8,3,7);w(&r,PORT+52,2,1);w(&r,PORT+54,2,1);
    /* round rectangle 8 x 4 with 4 x 2 corners (ovalH 2 at SP, ovalW 4, then the rectangle): rows 0 and 3 inset by 1 */
    memset(mem+PIX,9,32);CHECK(SH(0xa8b1,(w(&r,SP,2,2),w(&r,SP+2,2,4),w(&r,SP+4,4,RECT)))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+8);
    ROW(0,1,7,3,7);ROW(1,0,8,3,7);ROW(2,0,8,3,7);ROW(3,1,7,3,7);
    memset(mem+PIX,9,32);CHECK(SH(0xa8b0,(w(&r,SP,2,2),w(&r,SP+2,2,4),w(&r,SP+4,4,RECT)))==1);                         /* FrameRoundRect */
    ROW(0,1,7,3,7);
    memset(mem+PIX,9,32);CHECK(SH(0xa8b2,(w(&r,SP,2,2),w(&r,SP+2,2,4),w(&r,SP+4,4,RECT)))==1);ROW(0,1,7,3,7);        /* EraseRoundRect */
    memset(mem+PIX,9,32);CHECK(SH(0xa8b3,(w(&r,SP,2,2),w(&r,SP+2,2,4),w(&r,SP+4,4,RECT)))==1);CHECK(mem[PIX]==9&&mem[PIX+1]==(9^255));   /* InvertRoundRect */
    memset(mem+PIX,9,32);CHECK(SH(0xa8b4,(w(&r,SP,4,P+20),w(&r,SP+4,2,2),w(&r,SP+6,2,4),w(&r,SP+8,4,RECT)))==1&&mac_cpu_get(r.cpu,MAC_A7)==SP+12);ROW(0,1,7,3,7); /* FillRoundRect */
    /* a clipped shape: only what is inside the clip rectangle is drawn */
    memset(mem+PIX,9,32);rect(&r,R+2,1,0,3,4);CHECK(SH(0xa8b8,w(&r,SP,4,RECT))==1);CHECK(mem[PIX+3]==9&&mem[PIX+8+0]==3&&mem[PIX+8+4]==9&&mem[PIX+3*8+3]==9);rect(&r,R+2,0,0,4,8);
    /* a hidden pen draws no frame (but paints still draw); a negative pen size halts, as for rectangles */
    w(&r,PORT+66,2,0xffff);memset(mem+PIX,9,32);CHECK(SH(0xa8b7,w(&r,SP,4,RECT))==1&&mem[PIX+2]==9);w(&r,PORT+66,2,0);
    CHECK(SH(0xa8b8,w(&r,SP,4,RECT))==1&&mem[PIX+2]==3);
    mac_cpu_destroy(r.cpu);
    /* neighbours are not ours; bad pointers and stacks halt without drawing */
    r=setup();CHECK(mac_shapes_service(&r,0xa8b5)==0&&mac_shapes_service(&r,0xa8b6)==0&&mac_shapes_service(&r,0xa8af)==0&&mac_shapes_service(&r,0xa8bc)==0&&mac_shapes_service(&r,0xa8a1)==0);mac_cpu_destroy(r.cpu);
    for(int error=0;error<7;error++){
        r=setup();mem=mac_cpu_ram(r.cpu);memset(mem+PIX,99,32);mac_cpu_set(r.cpu,MAC_A7,SP);w(&r,SP,4,RECT);
        if(error==0)w(&r,SP,4,0);
        if(error==1)w(&r,SP,4,8190+4);
        if(error==2)w(&r,SP,4,RECT+1);
        if(error==3)mac_cpu_set(r.cpu,MAC_A7,SP+1);
        if(error==4)w(&r,PORT+52,2,0xffff);
        if(error==5)w(&r,0x80,4,0);
        if(error==6)rect(&r,RECT,0,0,2049,8);
        CHECK(mac_shapes_service(&r,error==4?0xa8b7:0xa8b8)==-1&&mac_cpu_halted(r.cpu));                              /* error 4: a negative pen size, which only a frame reads */
        for(int i=0;i<32;i++)CHECK(mem[PIX+i]==99);
        mac_cpu_destroy(r.cpu);
    }
    r=setup();mac_cpu_halt(r.cpu,"x");CHECK(mac_shapes_service(&r,0xa8b8)==-1);mac_cpu_destroy(r.cpu);
    printf("Color raster checks: %s\n",failures?"FAILED":"ok");return failures!=0;
}
