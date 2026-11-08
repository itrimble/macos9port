/* Colour icons: parsing and corrupted resources, GetCIcon / PlotCIcon / DisposeCIcon, masking, scaling, clipping and palette matching. */
#include "mac_cicn.h"
#include "mac_window_int.h"
#include "mac_resource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define SP 0x8000u
#define STORAGE 0x700000u
#define RECT 0x4300u
static uint8_t fork_image[2048], icon[400], big[70000];
static size_t icon_size;

static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v){CHECK(!mac_cpu_write(rt->cpu,p,n,v));}
static uint32_t raw(MacRuntime *rt,uint32_t p,unsigned n){const uint8_t *r=mac_cpu_ram(rt->cpu);uint32_t v=0;for(unsigned i=0;i<n;i++)v=v<<8|r[p+i];return v;}
static void put16(uint8_t *p,unsigned x){p[0]=(uint8_t)(x>>8);p[1]=(uint8_t)x;}
static void put32(uint8_t *p,uint32_t x){put16(p,x>>16);put16(p+2,x&0xffff);}
/* A general icon: w x h pixels of `depth` bits, row sizes as given, `entries` ColorSpecs (entry 0 red, entry 1 blue, value = position unless overridden afterwards). The
 * pixel in column c, row r has value (c+r)&1, except that row 7 of the default icon is value 2 (not in the table). The mask keeps the left half of the columns. */
static size_t build(uint8_t *d,size_t cap,unsigned w,unsigned h,unsigned depth,unsigned pix_row,unsigned mask_row,unsigned bmap_row,unsigned entries){
    size_t total=82+(size_t)mask_row*h+(size_t)bmap_row*h+8+(size_t)entries*8+(size_t)pix_row*h;CHECK(total<=cap);memset(d,0,total);
    put16(d+4,0x8000|pix_row);put16(d+10,h);put16(d+12,w);put32(d+22,0x480000);put32(d+26,0x480000);put16(d+32,depth);put16(d+34,1);put16(d+36,depth);
    put16(d+54,mask_row);put16(d+60,h);put16(d+62,w);put16(d+68,bmap_row);put16(d+74,h);put16(d+76,w);
    for(unsigned r=0;r<h;r++)for(unsigned c=0;c<w&&c<8*mask_row;c++)if(c<(w+1)/2)d[82+r*mask_row+c/8]|=(uint8_t)(0x80>>(c&7));
    size_t at=82+(size_t)mask_row*h+(size_t)bmap_row*h;put16(d+at+6,entries-1);at+=8;
    for(unsigned i=0;i<entries;i++){put16(d+at+i*8,i);if(i==0)put16(d+at+i*8+2,0xffff);else if(i==1)put16(d+at+i*8+6,0xffff);}
    at+=(size_t)entries*8;
    for(unsigned r=0;r<h;r++)for(unsigned c=0;c<w;c++){unsigned v=((c+r)&1)?1u:0u;if(w==8&&h==8&&r==7)v=2;size_t bit=(size_t)c*depth;if(depth<8&&bit/8<pix_row)d[at+(size_t)r*pix_row+bit/8]|=(uint8_t)(v<<(8-depth-(bit&7)));else if(depth==8)d[at+(size_t)r*pix_row+c]=(uint8_t)v;}
    return total;
}
static void build_icon(void){icon_size=build(icon,sizeof icon,8,8,4,4,1,1,2);}
static size_t build_fork(size_t size){
    enum{DATA=256};memset(fork_image,0,sizeof fork_image);const unsigned map=DATA+4+(unsigned)size;
    put32(fork_image+DATA,(uint32_t)size);memcpy(fork_image+DATA+4,icon,size);
    put32(fork_image,DATA);put32(fork_image+4,map);put32(fork_image+8,4+(uint32_t)size);put32(fork_image+12,50);
    put16(fork_image+map+24,28);put16(fork_image+map+26,50);put16(fork_image+map+28,0);put32(fork_image+map+30,0x6369636e);put16(fork_image+map+34,0);put16(fork_image+map+36,10);
    put16(fork_image+map+38,128);put16(fork_image+map+40,0xffff);
    return map+50;
}
static MacRuntime fresh(void) {
    MacRuntime rt={0};rt.cpu=mac_cpu_create(MAC_RUNTIME_RAM);CHECK(rt.cpu);
    rt.heap_next=0x100000;rt.heap_limit=0x600000;rt.qd_globals=0x7000;rt.modifiers=0x80;
    CHECK(mac_color_init(&rt)==1);mac_cpu_set(rt.cpu,MAC_A7,SP);CHECK(mac_toolbox_service(&rt,0xa912)==1);
    mac_cpu_set(rt.cpu,MAC_A7,SP);wr(&rt,SP,4,0x12345678);wr(&rt,SP+4,2,12);
    wr(&rt,SP+6,4,0xffffffff);wr(&rt,SP+10,2,12);wr(&rt,SP+12,2,0x0100);
    wr(&rt,SP+14,4,0x4001);wr(&rt,SP+18,4,0x4100);wr(&rt,SP+22,4,STORAGE);
    memcpy(mac_cpu_ram(rt.cpu)+0x4001,"\004Test",5);
    wr(&rt,0x4100,2,40);wr(&rt,0x4102,2,50);wr(&rt,0x4104,2,300);wr(&rt,0x4106,2,450);
    CHECK(mac_window_service(&rt,0xaa45)==1);
    char err[200];CHECK(!mac_resource_open(&rt.fork,fork_image,build_fork(icon_size),err,sizeof err));
    return rt;
}
static uint32_t getcicon(MacRuntime *rt,int id){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,2,(uint16_t)id);wr(rt,SP+2,4,0xdead);CHECK(mac_cicn_service(rt,0xaa1e)==1&&mac_cpu_get(rt->cpu,MAC_A7)==SP+2);return raw(rt,SP+2,4);}
static int plot(MacRuntime *rt,int t,int l,int b,int r,uint32_t h){
    wr(rt,RECT,2,(uint16_t)t);wr(rt,RECT+2,2,(uint16_t)l);wr(rt,RECT+4,2,(uint16_t)b);wr(rt,RECT+6,2,(uint16_t)r);
    mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,h);wr(rt,SP+4,4,RECT);int rc=mac_cicn_service(rt,0xaa1f);if(rc==1)CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP+8);return rc;
}
static int dispose(MacRuntime *rt,uint32_t h){mac_cpu_set(rt->cpu,MAC_A7,SP);wr(rt,SP,4,h);int rc=mac_cicn_service(rt,0xaa25);if(rc==1)CHECK(mac_cpu_get(rt->cpu,MAC_A7)==SP+4);return rc;}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static uint8_t *px(MacRuntime *rt,int x,int y){return mac_cpu_ram(rt->cpu)+rt->screen_base+(size_t)y*rt->screen_row_bytes+(size_t)x;}
static unsigned nearest(MacRuntime *rt,unsigned r,unsigned g,unsigned b){unsigned best=0,bd=~0u;for(unsigned k=0;k<256;k++){uint32_t c=rt->palette[k];int dr=(int)((c>>16)&255)-(int)r,dg=(int)((c>>8)&255)-(int)g,db=(int)(c&255)-(int)b;unsigned d=(unsigned)(dr*dr+dg*dg+db*db);if(d<bd){bd=d;best=k;}}return best;}
static void fill(MacRuntime *rt,int x0,int y0,int x1,int y1,uint8_t v){for(int y=y0;y<y1;y++)memset(px(rt,x0,y),v,(size_t)(x1-x0));}

int main(void){
    build_icon();
    /* Parsing: a good icon, then each field corrupted in turn. */
    {MacCicn c;const char *why;CHECK(mac_cicn_parse(icon,icon_size,&c,&why)==0 && c.width==8 && c.height==8 && c.depth==4 && c.pix_row==4 && c.mask_row==1 && c.colors==2 && c.pixels==icon+82+8+8+8+16);
     CHECK(mac_cicn_parse(icon,icon_size+10,&c,&why)==0);                                                  /* trailing bytes are ignored */
     static const struct{unsigned at,len,val;}bad[]={{4,2,0x0004},{4,2,0x8000},{4,2,0xbfff},{10,2,0},{12,2,0},{12,2,300},{10,2,0xff00},{16,2,1},{30,2,1},{32,2,3},{32,2,16},{34,2,3},{36,2,8},{54,2,0xffff},{54,2,0},{68,2,0xffff}};
     for(unsigned k=0;k<sizeof bad/sizeof *bad;k++){uint8_t t[400];memcpy(t,icon,sizeof t);if(bad[k].at==54&&bad[k].val==0)continue;put16(t+bad[k].at,bad[k].val);
         int rc=mac_cicn_parse(t,icon_size,&c,&why);
         if(bad[k].at==54&&bad[k].val==0){CHECK(rc==0);}else CHECK(rc==-1&&why);}
     for(size_t n=0;n<icon_size;n++)CHECK(mac_cicn_parse(icon,n,&c,&why)==-1);   /* every truncation fails */
     {uint8_t t[400];memcpy(t,icon,sizeof t);put16(t+82+16+6,0xffff);CHECK(mac_cicn_parse(t,icon_size,&c,&why)==-1);}   /* a colour table of 65536 entries */
     {uint8_t t[400];memcpy(t,icon,sizeof t);put16(t+82+16+6,200);CHECK(mac_cicn_parse(t,icon_size,&c,&why)==-1);}       /* a table that runs past the data */
     {uint8_t t[400];memcpy(t,icon,sizeof t);put16(t+54,0);CHECK(mac_cicn_parse(t,icon_size,&c,&why)==0 && !c.mask);}      /* no mask: all opaque (and the data starts earlier, so the table is wrong) */
    }
    /* Limits and floors (each icon is built whole, so only the field under test is wrong). */
    {MacCicn c;const char *why;size_t n;
     n=build(big,sizeof big,256,1,8,256,32,32,2);CHECK(mac_cicn_parse(big,n,&c,&why)==0 && c.width==256);
     n=build(big,sizeof big,257,1,8,257,33,33,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);
     n=build(big,sizeof big,8,1,16,16,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);                      /* 16-bit pixels */
     n=build(big,sizeof big,8,1,1,1,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==0 && c.depth==1);
     n=build(big,sizeof big,3,1,4,2,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==0);                          /* 3 pixels of 4 bits need 2 bytes */
     n=build(big,sizeof big,3,1,4,1,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);
     n=build(big,sizeof big,9,1,8,9,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);                         /* 9 mask bits need 2 bytes */
     n=build(big,sizeof big,9,1,8,9,2,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==0);
     n=build(big,sizeof big,8,1,8,5000,1,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);                      /* rowBytes beyond the limit even though the data is there */
     n=build(big,sizeof big,8,1,8,8,5000,1,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);
     n=build(big,sizeof big,8,1,8,8,1,5000,2);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);
     n=build(big,sizeof big,8,1,8,8,1,1,256);CHECK(mac_cicn_parse(big,n,&c,&why)==0 && c.colors==256);
     n=build(big,sizeof big,8,1,8,8,1,1,257);CHECK(mac_cicn_parse(big,n,&c,&why)==-1);
    }
    /* GetCIcon: a missing id is nil with resNotFound; a found one is a live handle. */
    MacRuntime rt=fresh();
    CHECK(getcicon(&rt,999)==0 && (int16_t)raw(&rt,0xa60,2)==-192);
    uint32_t h=getcicon(&rt,128);CHECK(h && !(h&1) && raw(&rt,0xa60,2)==0 && !memcmp(mac_cpu_ram(rt.cpu)+raw(&rt,h,4),icon,icon_size));
    /* PlotCIcon: opaque pixels in the nearest palette colours, masked pixels and the surroundings untouched. Local (10,10) is screen (60,50). */
    unsigned red=nearest(&rt,255,0,0),blue=nearest(&rt,0,0,255);CHECK(red!=blue);
    fill(&rt,40,40,120,80,0x55);
    CHECK(plot(&rt,10,10,18,18,h)==1);
    CHECK(*px(&rt,60,50)==red && *px(&rt,61,50)==blue && *px(&rt,62,50)==red && *px(&rt,60,51)==blue && *px(&rt,63,56)==blue && *px(&rt,63,57)==0x55 && *px(&rt,60,57)==0x55 && *px(&rt,64,50)==0x55 && *px(&rt,67,57)==0x55 && *px(&rt,59,50)==0x55 && *px(&rt,60,49)==0x55 && *px(&rt,60,58)==0x55);
    /* Scaling (nearest neighbour): 16x16 doubles each pixel. */
    fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,26,26,h)==1);
    CHECK(*px(&rt,60,50)==red && *px(&rt,61,51)==red && *px(&rt,62,50)==blue && *px(&rt,63,51)==blue && *px(&rt,64,50)==red && *px(&rt,68,50)==0x55 && *px(&rt,60,66)==0x55);
    /* An empty or inverted rectangle draws nothing. */
    fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,10,18,h)==1 && plot(&rt,18,18,10,10,h)==1 && *px(&rt,60,50)==0x55);
    /* Clipping: local coordinates below 0 are outside the port (screen x<50, y<40). Icon column 2 lands on local 0. */
    fill(&rt,40,30,60,50,0x55);CHECK(plot(&rt,-2,-2,6,6,h)==1 && *px(&rt,50,40)==red && *px(&rt,51,40)==blue && *px(&rt,49,40)==0x55 && *px(&rt,50,39)==0x55 && *px(&rt,47,37)==0x55);
    /* Non-square: 16 wide, 8 tall: columns double, rows do not (row 1 starts with the second colour). */
    fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,18,26,h)==1 && *px(&rt,60,50)==red && *px(&rt,61,50)==red && *px(&rt,62,50)==blue && *px(&rt,60,51)==blue && *px(&rt,62,51)==red);
    /* An empty rectangle needs no port at all. */
    {uint32_t port=rd(&rt,rt.qd_globals,4);wr(&rt,rt.qd_globals,4,0);CHECK(plot(&rt,10,10,10,10,h)==1);wr(&rt,rt.qd_globals,4,port);}
    /* Palette ties: with exactly two entries equal to red, the first is used. */
    {uint32_t keep[256];memcpy(keep,rt.palette,sizeof keep);for(unsigned k=0;k<256;k++)rt.palette[k]=0x00202020u+k;rt.palette[200]=0x00ff0000;rt.palette[40]=0x00ff0000;rt.palette[90]=0x000000ff;
     fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,18,18,h)==1 && *px(&rt,60,50)==40 && *px(&rt,61,50)==90);memcpy(rt.palette,keep,sizeof keep);}
    /* Colour table entries are found by value (not position) unless the table is a device table. */
    {uint32_t p=raw(&rt,h,4);uint8_t *ct=mac_cpu_ram(rt.cpu)+p+82+8+8+8;uint8_t save[16];memcpy(save,ct,16);
     put16(ct,1);put16(ct+8,0);                                                                              /* entry 0 (red) now has value 1, entry 1 (blue) value 0 */
     fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,18,18,h)==1 && *px(&rt,60,50)==blue && *px(&rt,61,50)==red);
     put16(ct-4,0x8000);                                                                     /* a device table: by position again */
     fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,18,18,h)==1 && *px(&rt,60,50)==red && *px(&rt,61,50)==blue);
     put16(ct-4,0);memcpy(ct,save,16);
     put16(ct,300);fill(&rt,40,40,120,80,0x55);CHECK(plot(&rt,10,10,18,18,h)==1 && *px(&rt,60,50)==0x55);     /* a value beyond 255 is ignored (ASan guards the table) */
     memcpy(ct,save,16);}
    /* Dispose: the handle dies, nil is fine, a second dispose halts. */
    CHECK(dispose(&rt,0)==1 && dispose(&rt,h)==1 && raw(&rt,h,4)==0 && dispose(&rt,h)==-1 && mac_cpu_halted(rt.cpu));
    mac_cpu_destroy(rt.cpu);
    /* Corrupted guest input: each halts and draws nothing. */
    for(unsigned k=0;k<11;k++){
        MacRuntime r=fresh();uint32_t ih=getcicon(&r,128),p=raw(&r,ih,4);int rc=0;fill(&r,40,40,120,80,0x55);
        switch(k){
        case 0:rc=plot(&r,10,10,18,18,0);break;                                                              /* nil */
        case 1:rc=plot(&r,10,10,18,18,ih+1);break;                                                           /* odd */
        case 2:rc=plot(&r,10,10,18,18,0x4800);break;                                                         /* not in the table */
        case 3:memcpy(mac_cpu_ram(r.cpu)+p+0x400,mac_cpu_ram(r.cpu)+p,icon_size);wr(&r,ih,4,p+0x400);rc=plot(&r,10,10,18,18,ih);break;   /* master pointer moved to a valid copy */
        case 4:put16(mac_cpu_ram(r.cpu)+p+4,0xffff);rc=plot(&r,10,10,18,18,ih);break;                       /* the program corrupts rowBytes after GetCIcon */
        case 5:put16(mac_cpu_ram(r.cpu)+p+12,0x7fff);rc=plot(&r,10,10,18,18,ih);break;                      /* ... the bounds */
        case 6:put16(mac_cpu_ram(r.cpu)+p+82+16+6,0xffff);rc=plot(&r,10,10,18,18,ih);break;                 /* ... the colour table size */
        case 7:{for(size_t i=0;i<r.handle_count;i++)if(r.handles[i].handle==ih)r.handles[i].type=0x1234;}rc=plot(&r,10,10,18,18,ih);break;   /* a handle of another kind */
        case 8:wr(&r,RECT,2,0);mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,4,ih);wr(&r,SP+4,4,(uint32_t)MAC_RUNTIME_RAM-4);rc=mac_cicn_service(&r,0xaa1f);break;   /* rectangle beyond RAM */
        case 9:r.current_port=0;wr(&r,r.qd_globals,4,0);rc=plot(&r,10,10,18,18,ih);break;                    /* no current port */
        default:wr(&r,STORAGE+6,2,0x0004);rc=plot(&r,10,10,18,18,ih);break;                                    /* a basic (non-colour) port */
        }
        if(!(rc==-1 && mac_cpu_halted(r.cpu) && *px(&r,60,50)==0x55))fprintf(stderr,"case %u rc=%d\n",k,rc);CHECK(rc==-1 && mac_cpu_halted(r.cpu) && *px(&r,60,50)==0x55);mac_cpu_destroy(r.cpu);
    }
    /* A resource that fails validation halts at GetCIcon, and a stack that does not fit halts. */
    {size_t keep=icon_size;put16(icon+12,0);MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,SP);wr(&r,SP,2,128);CHECK(mac_cicn_service(&r,0xaa1e)==-1&&mac_cpu_halted(r.cpu));mac_cpu_destroy(r.cpu);icon_size=keep;}
    build_icon();{MacRuntime r=fresh();mac_cpu_set(r.cpu,MAC_A7,(uint32_t)MAC_RUNTIME_RAM-2);CHECK(mac_cicn_service(&r,0xaa1e)==-1);mac_cpu_destroy(r.cpu);}
    CHECK(mac_cicn_service(&(MacRuntime){0},0xaa20)==0);
    puts("Colour icon checks passed");return 0;
}
