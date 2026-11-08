#include "mac_runtime.h"
#include "mac_shape.h"

#include <stdio.h>
#include <string.h>

int mac_raster_service(MacRuntime *rt, uint16_t opcode);
int mac_shapes_service(MacRuntime *rt, uint16_t opcode);
static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);fails++;} } while(0)
enum { PORT=0x700000, RECT=0x600000, STACK=0xf00000, AUX=0x600100 };

static void rect(MacRuntime *rt,uint32_t p,int t,int l,int b,int r) {
    mac_cpu_write(rt->cpu,p,2,(uint16_t)t);mac_cpu_write(rt->cpu,p+2,2,(uint16_t)l);
    mac_cpu_write(rt->cpu,p+4,2,(uint16_t)b);mac_cpu_write(rt->cpu,p+6,2,(uint16_t)r);
}

static int call(MacRuntime *rt,uint16_t op,uint32_t arg) {
    mac_cpu_set(rt->cpu,MAC_A7,STACK);
    mac_cpu_write(rt->cpu,STACK,4,arg);
    return mac_raster_service(rt,op);
}

static void clear(MacRuntime *rt) { memset(mac_cpu_ram(rt->cpu)+rt->screen_base,0,80*480); }
static unsigned byte(MacRuntime *rt,unsigned y,unsigned x) {return mac_cpu_ram(rt->cpu)[rt->screen_base+y*80+x];}

static int init(MacRuntime *rt) {
    memset(rt,0,sizeof *rt);rt->cpu=mac_cpu_create(MAC_RUNTIME_RAM);
    if(!rt->cpu)return -1;
    rt->heap_next=0x900000;rt->heap_limit=0xd00000;
    mac_cpu_set(rt->cpu,MAC_A5,0x800000);mac_cpu_set(rt->cpu,MAC_A7,STACK);
    mac_cpu_write(rt->cpu,STACK,4,0x7ffffc);
    CHECK(mac_quickdraw_service(rt,0xa86e)==1);
    mac_cpu_set(rt->cpu,MAC_A7,STACK);mac_cpu_write(rt->cpu,STACK,4,PORT);
    CHECK(mac_quickdraw_service(rt,0xa86f)==1);
    return 0;
}

int main(void) {
    MacRuntime rt;
    if(init(&rt))return 1;
    rect(&rt,RECT,0,0,1,1);CHECK(call(&rt,0xa8a2,RECT)==1);
    CHECK(byte(&rt,0,0)==0x80 && byte(&rt,0,1)==0);
    CHECK(mac_cpu_get(rt.cpu,MAC_A7)==STACK+4);
    rect(&rt,RECT,0,7,1,9);CHECK(call(&rt,0xa8a2,RECT)==1);
    CHECK(byte(&rt,0,0)==0x81 && byte(&rt,0,1)==0x80);
    CHECK(call(&rt,0xa8a4,RECT)==1);CHECK(byte(&rt,0,0)==0x80 && byte(&rt,0,1)==0);
    rect(&rt,RECT,-10,-10,1,1);CHECK(call(&rt,0xa8a3,RECT)==1);CHECK(byte(&rt,0,0)==0);
    clear(&rt);rect(&rt,RECT,0,0,5,5);CHECK(call(&rt,0xa8a1,RECT)==1);
    CHECK(byte(&rt,0,0)==0xf8 && byte(&rt,1,0)==0x88 && byte(&rt,4,0)==0xf8);
    CHECK(byte(&rt,5,0)==0);
    clear(&rt);CHECK(call(&rt,0xa89b,0x00020002)==1);CHECK(call(&rt,0xa8a1,RECT)==1);
    CHECK(byte(&rt,1,0)==0xf8 && byte(&rt,2,0)==0xd8);
    CHECK(call(&rt,0xa89e,0)==1);
    CHECK(mac_cpu_read(rt.cpu,PORT+52,4)==0x00010001);

    const unsigned truth[8][4]={{0,0,1,1},{0,1,1,1},{0,1,1,0},{0,1,0,0},
                                {1,1,0,0},{1,1,0,1},{1,0,0,1},{0,0,0,1}};
    rect(&rt,RECT,0,0,1,1);
    for(unsigned mode=0;mode<8;mode++)for(unsigned source=0;source<2;source++)for(unsigned old=0;old<2;old++) {
        clear(&rt);mac_cpu_ram(rt.cpu)[rt.screen_base]=(uint8_t)(old?0x80:0);
        memset(mac_cpu_ram(rt.cpu)+AUX+1,source?255:0,8);
        CHECK(call(&rt,0xa89d,AUX+1)==1);
        CHECK(call(&rt,0xa89c,(mode+8)<<16)==1);
        CHECK(call(&rt,0xa8a2,RECT)==1);
        CHECK(byte(&rt,0,0)==(truth[mode][source*2+old]?0x80:0));
    }
    CHECK(call(&rt,0xa89e,0)==1);

    /* Copy pattern bytes before rasterization, including source overlapping
     * the destination bitmap. Filling may never change later pattern rows. */
    clear(&rt);memset(mac_cpu_ram(rt.cpu)+AUX+1,0xaa,8);
    rect(&rt,RECT,0,0,8,8);
    mac_cpu_set(rt.cpu,MAC_A7,STACK);mac_cpu_write(rt.cpu,STACK,4,AUX+1);
    mac_cpu_write(rt.cpu,STACK+4,4,RECT);
    CHECK(mac_raster_service(&rt,0xa8a5)==1);
    CHECK(byte(&rt,0,0)==0xaa && byte(&rt,7,0)==0xaa);
    CHECK(call(&rt,0xa89d,AUX+1)==1);
    CHECK(call(&rt,0xa89c,10u<<16)==1);
    CHECK(call(&rt,0xa8a2,RECT)==1);CHECK(byte(&rt,0,0)==0);
    CHECK(call(&rt,0xa8a2,RECT)==1);CHECK(byte(&rt,0,0)==0xaa);
    clear(&rt);
    for(unsigned i=0;i<8;i++)mac_cpu_ram(rt.cpu)[rt.screen_base+i]=(uint8_t)(0x80u>>i);
    rect(&rt,RECT,0,0,8,16);
    mac_cpu_set(rt.cpu,MAC_A7,STACK);mac_cpu_write(rt.cpu,STACK,4,rt.screen_base);
    mac_cpu_write(rt.cpu,STACK+4,4,RECT);
    CHECK(mac_raster_service(&rt,0xa8a5)==1);
    CHECK(byte(&rt,0,0)==0x80 && byte(&rt,0,1)==0x80 && byte(&rt,1,0)==0x40 && byte(&rt,7,0)==1);
    CHECK(call(&rt,0xa89e,0)==1);

    clear(&rt);rect(&rt,RECT,1,1,3,3);CHECK(call(&rt,0xa87b,RECT)==1);
    rect(&rt,RECT,0,0,8,8);CHECK(call(&rt,0xa8a2,RECT)==1);
    CHECK(byte(&rt,0,0)==0 && byte(&rt,1,0)==0x60 && byte(&rt,2,0)==0x60 && byte(&rt,3,0)==0);
    mac_cpu_write(rt.cpu,AUX,4,AUX+16);mac_cpu_write(rt.cpu,AUX+16,2,10);
    rect(&rt,AUX+18,0,0,0,0);CHECK(call(&rt,0xa87a,AUX)==1);
    CHECK(mac_cpu_read(rt.cpu,AUX+18,4)==0x00010001);
    CHECK(mac_cpu_read(rt.cpu,AUX+22,4)==0x00030003);
    rect(&rt,AUX+18,0,0,2,2);CHECK(call(&rt,0xa879,AUX)==1);
    clear(&rt);CHECK(call(&rt,0xa8a2,RECT)==1);
    CHECK(byte(&rt,0,0)==0xc0 && byte(&rt,1,0)==0xc0 && byte(&rt,2,0)==0);
    rect(&rt,RECT,0,0,480,640);CHECK(call(&rt,0xa87b,RECT)==1);
    clear(&rt);CHECK(call(&rt,0xa893,0x00060000)==1);
    CHECK(call(&rt,0xa891,0x00060003)==1);
    CHECK(byte(&rt,6,0)==0xf0 && byte(&rt,5,0)==0);
    CHECK(call(&rt,0xa89a,AUX)==1);CHECK(mac_cpu_read(rt.cpu,AUX,4)==0x00060003);
    CHECK(call(&rt,0xa896,0)==1);CHECK(call(&rt,0xa891,0x00060007)==1);
    CHECK(byte(&rt,6,0)==0xf0);CHECK(mac_cpu_read(rt.cpu,PORT+48,4)==0x00060007);
    CHECK(call(&rt,0xa897,0)==1);

    /* Guest bitmap origin may be negative; storage offsets remain nonnegative. */
    clear(&rt);rect(&rt,PORT+8,-2,-8,478,632);rect(&rt,PORT+16,-2,-8,478,632);
    uint32_t vr=mac_cpu_read(rt.cpu,mac_cpu_read(rt.cpu,PORT+24,4),4);
    rect(&rt,vr+2,-2,-8,478,632);rect(&rt,RECT,-2,-8,478,632);
    CHECK(call(&rt,0xa87b,RECT)==1);rect(&rt,RECT,-2,-8,-1,-7);
    CHECK(call(&rt,0xa8a2,RECT)==1);CHECK(byte(&rt,0,0)==0x80);
    CHECK(call(&rt,0xa8ec,0)==0); /* No unsupported CopyBits success. */
    mac_cpu_write(rt.cpu,vr,2,12);
    CHECK(call(&rt,0xa8a2,RECT)==-1);CHECK(strstr(mac_cpu_error(rt.cpu),"Complex")!=NULL);
    mac_runtime_destroy(&rt);

    if(init(&rt))return 1;
    CHECK(call(&rt,0xa89c,64u<<16)==-1);
    CHECK(strstr(mac_cpu_error(rt.cpu),"transfer mode")!=NULL);mac_runtime_destroy(&rt);
    if(init(&rt))return 1;
    CHECK(call(&rt,0xa893,0)==1);CHECK(call(&rt,0xa891,0x00010001)==-1);
    CHECK(strstr(mac_cpu_error(rt.cpu),"Diagonal")!=NULL);mac_runtime_destroy(&rt);
    if(init(&rt))return 1;
    mac_cpu_write(rt.cpu,PORT+2,4,0xfffffe);rect(&rt,RECT,0,0,8,8);
    CHECK(call(&rt,0xa8a2,RECT)==-1);CHECK(mac_cpu_halted(rt.cpu));mac_runtime_destroy(&rt);
    /* A color port (portVersion 0xC000 at +6): the clip traps work, drawing and pen traps still refuse. */
    if(init(&rt))return 1;
    mac_cpu_write(rt.cpu,PORT+6,2,0xc000);
    rect(&rt,RECT,3,4,50,60);CHECK(call(&rt,0xa87b,RECT)==1);
    {   uint32_t clip=mac_cpu_read(rt.cpu,mac_cpu_read(rt.cpu,PORT+28,4),4);
        CHECK(mac_cpu_read(rt.cpu,clip+2,4)==0x00030004 && mac_cpu_read(rt.cpu,clip+6,4)==0x0032003c);
        CHECK(mac_cpu_get(rt.cpu,MAC_A7)==STACK+4);
        mac_cpu_write(rt.cpu,AUX,4,AUX+16);mac_cpu_write(rt.cpu,AUX+16,2,10);rect(&rt,AUX+18,0,0,0,0);
        CHECK(call(&rt,0xa87a,AUX)==1);                                  /* GetClip copies the clip out */
        CHECK(mac_cpu_read(rt.cpu,AUX+18,4)==0x00030004 && mac_cpu_read(rt.cpu,AUX+22,4)==0x0032003c);
        rect(&rt,AUX+18,7,8,9,10);CHECK(call(&rt,0xa879,AUX)==1);        /* SetClip copies one in */
        CHECK(mac_cpu_read(rt.cpu,clip+2,4)==0x00070008 && mac_cpu_read(rt.cpu,clip+6,4)==0x0009000a); }
    CHECK(!mac_cpu_halted(rt.cpu));
    const unsigned refused[]={0xa8a2,0xa8a1,0xa891,0xa89b,0xa89c,0xa89d,0xa87c};
    for(unsigned i=0;i<sizeof refused/sizeof *refused;i++){
        MacRuntime c;if(init(&c))return 1;mac_cpu_write(c.cpu,PORT+6,2,0xc000);
        rect(&c,RECT,0,0,8,8);CHECK(call(&c,refused[i],RECT)==-1);
        CHECK(mac_cpu_halted(c.cpu) && strstr(mac_cpu_error(c.cpu),"color ports")!=NULL);mac_runtime_destroy(&c);
    }
    /* Ovals and round rectangles on the 1-bit port. A 4 x 4 oval at (10, 10) is 2, 4, 4, 2 wide: x 11-12, 10-13, 10-13, 11-12 (byte 1 holds x 8-15). */
    MacRuntime sh;if(init(&sh))return 1;                                                                                       /* a fresh 1-bit port: the one above was switched to colour */
    #define SHP(op,setup) (mac_cpu_set(sh.cpu,MAC_A7,STACK),setup,mac_shapes_service(&sh,(op)))
    clear(&sh);rect(&sh,RECT,10,10,14,14);
    CHECK(SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&mac_cpu_get(sh.cpu,MAC_A7)==STACK+4);
    CHECK(byte(&sh,10,1)==0x18&&byte(&sh,11,1)==0x3c&&byte(&sh,12,1)==0x3c&&byte(&sh,13,1)==0x18&&byte(&sh,9,1)==0&&byte(&sh,14,1)==0&&byte(&sh,10,0)==0);   /* PaintOval */
    CHECK(SHP(0xa8b9,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,11,1)==0&&byte(&sh,10,1)==0);                          /* EraseOval (background pattern white) */
    /* an 8 x 8 oval is 4, 6, 6, 8, 8, 6, 6, 4 wide; its 1-pixel frame leaves the ends of the middle rows (x 10 and 17) and the solid ends */
    clear(&sh);rect(&sh,RECT,10,10,18,18);CHECK(SHP(0xa8b7,mac_cpu_write(sh.cpu,STACK,4,RECT))==1);
    CHECK(byte(&sh,10,1)==0x0f&&byte(&sh,10,2)==0x00&&byte(&sh,11,1)==0x19&&byte(&sh,11,2)==0x80&&byte(&sh,12,1)==0x10&&byte(&sh,12,2)==0x80);
    CHECK(byte(&sh,13,1)==0x20&&byte(&sh,13,2)==0x40&&byte(&sh,14,1)==0x20&&byte(&sh,14,2)==0x40&&byte(&sh,15,1)==0x10&&byte(&sh,15,2)==0x80);
    CHECK(byte(&sh,16,1)==0x19&&byte(&sh,16,2)==0x80&&byte(&sh,17,1)==0x0f&&byte(&sh,9,1)==0&&byte(&sh,18,1)==0);
    CHECK(SHP(0xa8ba,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,10,1)==0x00&&byte(&sh,13,1)==0x1f&&byte(&sh,13,2)==0x80);   /* InvertOval flips every oval pixel: row 10 clears, row 13 (x 10 and 17 set) becomes x 11-16 */
    clear(&sh);rect(&sh,RECT,10,10,16,18);                                                                                    /* round rectangle 8 x 6 with 4 x 4 corners: first and last rows inset by 1 */
    CHECK(SHP(0xa8b1,(mac_cpu_write(sh.cpu,STACK,2,4),mac_cpu_write(sh.cpu,STACK+2,2,4),mac_cpu_write(sh.cpu,STACK+4,4,RECT)))==1&&mac_cpu_get(sh.cpu,MAC_A7)==STACK+8);
    CHECK(byte(&sh,10,1)==0x1f&&byte(&sh,10,2)==0x80&&byte(&sh,11,1)==0x3f&&byte(&sh,11,2)==0xc0&&byte(&sh,14,1)==0x3f&&byte(&sh,14,2)==0xc0&&byte(&sh,15,1)==0x1f&&byte(&sh,15,2)==0x80&&byte(&sh,9,1)==0&&byte(&sh,16,1)==0);
    clear(&sh);rect(&sh,RECT,10,10,14,14);mac_cpu_write(sh.cpu,PORT+66,2,0xffff);CHECK(SHP(0xa8b7,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,10,1)==0);   /* hidden pen: no frame */
    CHECK(SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,10,1)==0x18);                                           /* ... but a paint still draws */
    mac_cpu_write(sh.cpu,PORT+66,2,0);
    /* the frame of a round rectangle with a 2 x 2 pen: the outer shape minus the inset shape whose corners shrink by the pen (20 x 20, corners 8 x 8) */
    clear(&sh);rect(&sh,RECT,10,10,30,30);mac_cpu_write(sh.cpu,PORT+52,4,0x00020002);
    CHECK(SHP(0xa8b0,(mac_cpu_write(sh.cpu,STACK,2,8),mac_cpu_write(sh.cpu,STACK+2,2,8),mac_cpu_write(sh.cpu,STACK+4,4,RECT)))==1);
    {MacShape o,n;CHECK(mac_shape_build(&o,10,10,30,30,8,8)==0&&mac_shape_build(&n,12,12,28,28,4,4)==0);
     for(int y=8;y<32;y++)for(int x=8;x<32;x++){int want=mac_shape_contains(&o,x,y)&&!mac_shape_contains(&n,x,y);int got=(byte(&sh,y,x/8)>>(7-x%8))&1;if(want!=got){fprintf(stderr,"frame pixel %d,%d want %d\n",x,y,want);fails++;}}
     mac_shape_free(&o);mac_shape_free(&n);}
    mac_cpu_write(sh.cpu,PORT+52,4,0x00010001);
    /* the pen mode applies to paints and frames: patXor twice puts the pixels back, patCopy twice leaves them set */
    clear(&sh);rect(&sh,RECT,10,10,14,14);mac_cpu_write(sh.cpu,PORT+56,2,10);
    CHECK(SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,11,1)==0x3c);CHECK(SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,11,1)==0);
    mac_cpu_write(sh.cpu,PORT+56,2,8);CHECK(SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&SHP(0xa8b8,mac_cpu_write(sh.cpu,STACK,4,RECT))==1&&byte(&sh,11,1)==0x3c);
    mac_cpu_write(sh.cpu,PORT+52,2,0xffff);CHECK(SHP(0xa8b7,mac_cpu_write(sh.cpu,STACK,4,RECT))==-1&&mac_cpu_halted(sh.cpu));         /* negative pen size */
    mac_runtime_destroy(&sh);
    mac_runtime_destroy(&rt);
    printf("Mono raster checks: %s\n",fails?"FAILED":"ok");
    return fails?1:0;
}
