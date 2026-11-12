/* Offscreen graphics worlds on the 68K services: creation at every depth, the PixMap they present, GetGWorld/SetGWorld, UpdateGWorld, disposal, errors,
 * and CopyBits between a GWorld and the screen (same format raw, other formats through RGB). Pixels are written straight into the GWorld's storage,
 * because what is under test is the storage, the records and the transfer. */
#include "mac_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);fails++;}}while(0)
#define SP 0x8000u
#define BOUNDS 0x4000u
#define VAR 0x4100u
#define RECT 0x4200u
#define RECT2 0x4210u
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
int mac_copybits_service(MacRuntime*rt,uint16_t opcode);
static void rect(MacRuntime*r,uint32_t at,int t,int l,int b,int rr){wr(r,at,2,(uint16_t)t);wr(r,at+2,2,(uint16_t)l);wr(r,at+4,2,(uint16_t)b);wr(r,at+6,2,(uint16_t)rr);}
static int sel(MacRuntime*r,uint32_t selector){mac_cpu_set(r->cpu,MAC_A7,SP);mac_cpu_set(r->cpu,MAC_D0,selector);return mac_gworld_service(r,0xab1d);}
/* NewGWorld(depth, bounds): the QDErr; *out is the GWorld */
static int new_gw(MacRuntime*r,int depth,int t,int l,int b,int rr,uint32_t ctab,uint32_t*out){
    rect(r,BOUNDS,t,l,b,rr);wr(r,VAR,4,0xdeadbeef);
    wr(r,SP,4,0);wr(r,SP+4,4,0);wr(r,SP+8,4,ctab);wr(r,SP+12,4,BOUNDS);wr(r,SP+16,2,(uint16_t)depth);wr(r,SP+18,4,VAR);wr(r,SP+22,2,0x7777);
    if(sel(r,0x00160000)!=1)return 9999;
    *out=rd(r,VAR,4);return (int16_t)rd(r,SP+22,2);
}
static uint32_t pixmap_of(MacRuntime*r,uint32_t port){return rd(r,rd(r,port+2,4),4);}
static MacRuntime mk(void){
    MacRuntime r;memset(&r,0,sizeof r);r.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(r.cpu);
    r.heap_next=0x100000;r.heap_limit=0x600000;r.qd_globals=0x7000;
    CHECK(mac_color_init(&r)==1);
    return r;
}
/* a colour port over the screen, current */
static uint32_t screen_port(MacRuntime*r){
    uint32_t port=mac_runtime_alloc(r,108);mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,4,port);
    CHECK(mac_color_port_service(r,0xaa00)==1);wr(r,r->qd_globals,4,port);r->current_port=port;return port;
}
static int copy(MacRuntime*r,uint32_t srcbits,uint32_t dstbits,int mode){
    mac_cpu_set(r->cpu,MAC_A7,SP);wr(r,SP,4,0);wr(r,SP+4,2,(uint16_t)mode);wr(r,SP+6,4,RECT2);wr(r,SP+10,4,RECT);wr(r,SP+14,4,dstbits);wr(r,SP+18,4,srcbits);
    return mac_copybits_service(r,0xa8ec);
}
static uint8_t*ram(MacRuntime*r){return mac_cpu_ram(r->cpu);}

static void creation(void){
    MacRuntime r=mk();uint32_t g;
    static const struct{int depth;unsigned row;int dir;}d[]={{1,8,0},{2,16,0},{4,32,0},{8,60,0},{16,120,1},{32,240,1}};
    for(unsigned i=0;i<6;i++){
        CHECK(new_gw(&r,d[i].depth,10,20,50,80,0,&g)==0 && g && mac_cpu_get(r.cpu,MAC_A7)==SP+22);
        uint32_t pm=pixmap_of(&r,g);
        CHECK(rd(&r,g+6,2)==0xc000 && rd(&r,pm+4,2)==(0x8000u|d[i].row) && rd(&r,pm,4)>=0x100000);
        CHECK((int16_t)rd(&r,pm+6,2)==10 && (int16_t)rd(&r,pm+8,2)==20 && (int16_t)rd(&r,pm+10,2)==50 && (int16_t)rd(&r,pm+12,2)==80);
        CHECK(rd(&r,pm+32,2)==(unsigned)d[i].depth && rd(&r,pm+30,2)==(d[i].dir?16u:0u) && rd(&r,pm+34,2)==(d[i].dir?3u:1u));
        CHECK((int16_t)rd(&r,g+16,2)==10 && (int16_t)rd(&r,g+18,2)==20 && (int16_t)rd(&r,g+20,2)==50 && (int16_t)rd(&r,g+22,2)==80);   /* portRect */
        /* GetGWorldPixMap and GetPixBaseAddr */
        wr(&r,SP,4,g);wr(&r,SP+4,4,0);CHECK(sel(&r,0x00040017)==1 && rd(&r,SP+4,4)==rd(&r,g+2,4) && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
        wr(&r,SP,4,rd(&r,g+2,4));wr(&r,SP+4,4,0);CHECK(sel(&r,0x0004000f)==1 && rd(&r,SP+4,4)==rd(&r,pm,4));
        wr(&r,SP,4,rd(&r,g+2,4));wr(&r,SP+4,2,0);CHECK(sel(&r,0x00040001)==1 && rd(&r,SP+4,1)==1);                       /* LockPixels: TRUE */
        wr(&r,SP,4,rd(&r,g+2,4));CHECK(sel(&r,0x00040002)==1);                                                          /* UnlockPixels */
        wr(&r,SP,4,rd(&r,g+2,4));wr(&r,SP+4,2,0);CHECK(sel(&r,0x00040016)==1 && rd(&r,SP+4,1)==1);                       /* PixMap32Bit */
    }
    /* depth 0 means the screen's depth */
    CHECK(new_gw(&r,0,0,0,10,10,0,&g)==0 && rd(&r,pixmap_of(&r,g)+32,2)==8);
    /* errors leave the result nil: bad depth, empty or inverted bounds, too big */
    CHECK(new_gw(&r,3,0,0,10,10,0,&g)==-50 && g==0);
    CHECK(new_gw(&r,8,5,5,5,9,0,&g)==-50 && g==0 && new_gw(&r,8,5,5,9,5,0,&g)==-50);
    CHECK(new_gw(&r,32,0,0,30000,30000,0,&g)==-108 && g==0);
    /* a colour table supplied by the caller: a copy with its own seed */
    { uint32_t ch=mac_runtime_alloc(&r,4),cp=mac_runtime_alloc(&r,8+2*8);wr(&r,ch,4,cp);wr(&r,cp,4,77);wr(&r,cp+6,2,1);
      wr(&r,cp+8,2,0);wr(&r,cp+10,2,0xffff);wr(&r,cp+12,2,0xffff);wr(&r,cp+14,2,0xffff);wr(&r,cp+16,2,1);
      CHECK(new_gw(&r,1,0,0,8,8,ch,&g)==0);
      uint32_t tp=rd(&r,rd(&r,pixmap_of(&r,g)+42,4),4);CHECK(rd(&r,tp+6,2)==1 && rd(&r,tp+10,2)==0xffff && rd(&r,tp+16,2)==1 && rd(&r,tp,4)!=77);
      CHECK(new_gw(&r,8,0,0,8,8,ch,&g)==0);                                    /* two entries at 8 bits is fine */
      wr(&r,cp+6,2,300);CHECK(new_gw(&r,8,0,0,8,8,ch,&g)==-50);                /* a table that is not valid */
    }
    CHECK(!mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
}

static void ports_and_dispose(void){
    MacRuntime r=mk();uint32_t g,sp=screen_port(&r),got_port=0,got_dev=0;
    CHECK(new_gw(&r,8,0,0,16,16,0,&g)==0);
    /* GetGWorld/SetGWorld: the current port moves to the GWorld, the device stays the main device */
    wr(&r,SP,4,VAR+8);wr(&r,SP+4,4,VAR);CHECK(sel(&r,0x00080005)==1 && rd(&r,VAR,4)==sp && rd(&r,VAR+8,4)==r.main_device && mac_cpu_get(r.cpu,MAC_A7)==SP+8);
    wr(&r,SP,4,0);wr(&r,SP+4,4,g);CHECK(sel(&r,0x00080006)==1 && rd(&r,r.qd_globals,4)==g && r.current_port==g);
    wr(&r,SP,4,VAR+8);wr(&r,SP+4,4,VAR);CHECK(sel(&r,0x00080005)==1 && (got_port=rd(&r,VAR,4))==g && (got_dev=rd(&r,VAR+8,4))==r.main_device);
    wr(&r,SP,4,r.main_device);wr(&r,SP+4,4,sp);CHECK(sel(&r,0x00080006)==1 && rd(&r,r.qd_globals,4)==sp);
    /* UpdateGWorld: the same shape keeps the pixels; a new shape gets new storage and reports no flags */
    uint32_t pm=pixmap_of(&r,g),base=rd(&r,pm,4);
    rect(&r,BOUNDS,0,0,16,16);wr(&r,VAR,4,g);
    wr(&r,SP,4,0);wr(&r,SP+4,4,0);wr(&r,SP+8,4,0);wr(&r,SP+12,4,BOUNDS);wr(&r,SP+16,2,8);wr(&r,SP+18,4,VAR);wr(&r,SP+22,4,0x77777777);
    CHECK(sel(&r,0x00160003)==1 && rd(&r,SP+22,4)==0 && rd(&r,pm,4)==base && mac_cpu_get(r.cpu,MAC_A7)==SP+22);
    rect(&r,BOUNDS,0,0,32,48);
    wr(&r,SP+16,2,16);wr(&r,SP+22,4,0x77777777);
    CHECK(sel(&r,0x00160003)==1 && rd(&r,SP+22,4)==0 && rd(&r,pm,4)!=base && (int16_t)rd(&r,pm+12,2)==48 && rd(&r,pm+32,2)==16 && rd(&r,pm+30,2)==16);
    CHECK((int16_t)rd(&r,g+22,2)==48 && (int16_t)rd(&r,g+20,2)==32);
    rect(&r,BOUNDS,0,0,0,0);wr(&r,SP+22,4,0);CHECK(sel(&r,0x00160003)==1 && rd(&r,SP+22,4)==0x80000000u);   /* empty bounds: gwFlagErr */
    /* DisposeGWorld */
    wr(&r,SP,4,g);CHECK(sel(&r,0x00040004)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+4);
    CHECK(!mac_cpu_halted(r.cpu));
    wr(&r,SP,4,g);CHECK(sel(&r,0x00040004)==-1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"not a GWorld"));
    mac_cpu_destroy(r.cpu);
    /* disposing the current port, and the rest of the refusals */
    r=mk();sp=screen_port(&r);CHECK(new_gw(&r,8,0,0,8,8,0,&g)==0);
    wr(&r,SP,4,0);wr(&r,SP+4,4,g);CHECK(sel(&r,0x00080006)==1);
    wr(&r,SP,4,g);CHECK(sel(&r,0x00040004)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);
    r=mk();screen_port(&r);CHECK(sel(&r,0x00040099)==-1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"Offscreen selector 00040099"));mac_cpu_destroy(r.cpu);
    r=mk();screen_port(&r);wr(&r,SP,4,0x4321);CHECK(sel(&r,0x00040017)==-1 && mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);   /* GetGWorldPixMap of a non-GWorld */
    r=mk();screen_port(&r);wr(&r,SP,4,0x4321);wr(&r,SP+4,4,0);CHECK(sel(&r,0x00040001)==0);mac_cpu_destroy(r.cpu);               /* LockPixels of a PixMap that is not a GWorld's: mac_pixels.c's */
    r=mk();screen_port(&r);CHECK(sel(&r,0x00080006)==-1);mac_cpu_destroy(r.cpu);                                              /* a stack that is too short is refused (arguments run past RAM) or the port is nil */
    /* many create/dispose rounds keep working (the Memory Manager here is a bump allocator, so a disposed block's address range is not reused: 20 x 40 KB is what they cost) */
    r=mk();screen_port(&r);
    for(unsigned i=0;i<20;i++){CHECK(new_gw(&r,32,0,0,100,100,0,&g)==0);wr(&r,SP,4,g);CHECK(sel(&r,0x00040004)==1);}
    mac_cpu_destroy(r.cpu);
    (void)got_port;(void)got_dev;
}

static void transfers(void){
    MacRuntime r=mk();uint32_t sp=screen_port(&r),g8,g16,g32,g1;
    CHECK(new_gw(&r,8,0,0,8,8,0,&g8)==0 && new_gw(&r,16,0,0,8,8,0,&g16)==0 && new_gw(&r,32,0,0,8,8,0,&g32)==0 && new_gw(&r,1,0,0,8,8,0,&g1)==0);
    wr(&r,r.qd_globals,4,sp);r.current_port=sp;
    uint32_t sbits=sp+2;      /* a colour port's portBits: the way to name its PixMap */
    unsigned char*m=ram(&r);
    /* 8-bit GWorld to the screen: same table seed, so the indices arrive untouched */
    { uint32_t pm=pixmap_of(&r,g8),b=rd(&r,pm,4);for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)m[b+y*8+x]=(unsigned char)(10*y+x+1);
      rect(&r,RECT,0,0,8,8);rect(&r,RECT2,100,200,108,208);
      CHECK(copy(&r,pm,sbits,0)==1 && mac_cpu_get(r.cpu,MAC_A7)==SP+22);
      int ok=1;for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)if(m[r.screen_base+(100+y)*640+200+x]!=10*y+x+1)ok=0;CHECK(ok);
      /* and back from the screen into the GWorld, scaled up 2x by CopyBits' own rules */
      memset(m+b,0,64);rect(&r,RECT,100,200,104,204);rect(&r,RECT2,0,0,8,8);
      CHECK(copy(&r,sbits,pm,0)==1 && m[b]==1 && m[b+1]==1 && m[b+2]==2 && m[b+8]==1 && m[b+16]==11);
    }
    /* 32-bit red to the 8-bit screen: the nearest colour of the screen's table */
    { uint32_t pm=pixmap_of(&r,g32),b=rd(&r,pm,4);for(unsigned i=0;i<64;i++){m[b+4*i]=0;m[b+4*i+1]=255;m[b+4*i+2]=0;m[b+4*i+3]=0;}
      rect(&r,RECT,0,0,8,8);rect(&r,RECT2,200,300,208,308);
      CHECK(copy(&r,pm,sbits,0)==1);
      uint32_t ct=rd(&r,r.screen_colors,4);unsigned idx=m[r.screen_base+200*640+300];
      CHECK(rd(&r,ct+10+idx*8,2)>0xc000 && rd(&r,ct+12+idx*8,2)<0x4000 && rd(&r,ct+14+idx*8,2)<0x4000);
      int same=1;for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)if(m[r.screen_base+(200+y)*640+300+x]!=idx)same=0;CHECK(same);
    }
    /* screen to 32-bit: the table's colour, exactly (8 bits per channel) */
    { uint32_t pm=pixmap_of(&r,g32),b=rd(&r,pm,4),ct=rd(&r,r.screen_colors,4);
      for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)m[r.screen_base+(50+y)*640+60+x]=(unsigned char)(y*8+x);
      rect(&r,RECT,50,60,58,68);rect(&r,RECT2,0,0,8,8);memset(m+b,0xee,256);
      CHECK(copy(&r,sbits,pm,0)==1);
      int ok=1;for(unsigned i=0;i<64;i++)for(unsigned k=0;k<3;k++)if(m[b+4*i+1+k]!=rd(&r,ct+10+i*8+2*k,2)>>8)ok=0;CHECK(ok && m[b]==0);
    }
    /* screen to 16-bit: 5 bits a channel */
    { uint32_t pm=pixmap_of(&r,g16),b=rd(&r,pm,4),ct=rd(&r,r.screen_colors,4);
      rect(&r,RECT,50,60,58,68);rect(&r,RECT2,0,0,8,8);CHECK(copy(&r,sbits,pm,0)==1);
      unsigned c0=rd(&r,ct+10+5*8,2)>>11,c1=rd(&r,ct+12+5*8,2)>>11,c2=rd(&r,ct+14+5*8,2)>>11;
      CHECK(((m[b+10]<<8|m[b+11]))==(int)(c0<<10|c1<<5|c2));
    }
    /* 1-bit to the screen: 1 is the port's foreground (black), 0 its background (white) */
    { uint32_t pm=pixmap_of(&r,g1),b=rd(&r,pm,4);for(unsigned y=0;y<8;y++)m[b+y*4]=(unsigned char)(y&1?0xff:0x00);
      rect(&r,RECT,0,0,8,8);rect(&r,RECT2,300,300,308,308);CHECK(copy(&r,pm,sbits,0)==1);
      unsigned dark=m[r.screen_base+301*640+300],light=m[r.screen_base+300*640+300];
      uint32_t ct=rd(&r,r.screen_colors,4);
      CHECK(rd(&r,ct+10+dark*8,2)<0x1000 && rd(&r,ct+10+light*8,2)>0xf000);
    }
    /* the screen into a 1-bit GWorld by luminance */
    { uint32_t pm=pixmap_of(&r,g1),b=rd(&r,pm,4);
      rect(&r,RECT,300,300,308,308);rect(&r,RECT2,0,0,8,8);CHECK(copy(&r,sbits,pm,0)==1 && m[b]==0x00 && m[b+4]==0xff);
    }
    /* modes other than srcCopy between different formats are named refusals */
    { uint32_t pm=pixmap_of(&r,g32);rect(&r,RECT,0,0,8,8);rect(&r,RECT2,0,0,8,8);CHECK(copy(&r,pm,sbits,1)==-1 && mac_cpu_halted(r.cpu) && strstr(mac_cpu_error(r.cpu),"srcCopy")); }
    mac_cpu_destroy(r.cpu);
}
int main(void){
    creation();ports_and_dispose();transfers();
    if(!fails)puts("Offscreen graphics world checks passed");
    return fails?1:0;
}
