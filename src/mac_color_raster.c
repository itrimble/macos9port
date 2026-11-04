#include "mac_runtime.h"
#include <limits.h>
#include <string.h>

/* Independent indexed-8 rectangular CPU rasterizer. Public interfaces:
 * https://leopard-adc.pepas.com/documentation/Carbon/Reference/QuickDraw_Ref/QuickDraw_Ref.pdf
 * CGrafPort/PixPat layouts: dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-269.html
 * Rectangular regions and old-style one-bit patterns only. No GPU guest drawing.
 */
typedef struct { int t, l, b, r; } Box;
static int fail(MacRuntime *rt, const char *s) { mac_cpu_halt(rt->cpu,s); return -1; }
static int valid(MacRuntime *rt,uint32_t p,size_t n,int aligned) {
    if ((aligned && (p&1)) || p>mac_cpu_ram_size(rt->cpu) || n>mac_cpu_ram_size(rt->cpu)-p)
        return fail(rt,"Invalid Color QuickDraw guest memory range.");
    return 1;
}
static uint32_t rd(MacRuntime *r,uint32_t p,unsigned n) { return mac_cpu_read(r->cpu,p,n); }
static void wr(MacRuntime *r,uint32_t p,unsigned n,uint32_t v) { mac_cpu_write(r->cpu,p,n,v); }
static int box(MacRuntime *r,uint32_t p,Box *b) {
    if (!p || valid(r,p,8,1)<0) return fail(r,"Invalid color rectangle.");
    *b=(Box){(int16_t)rd(r,p,2),(int16_t)rd(r,p+2,2),(int16_t)rd(r,p+4,2),(int16_t)rd(r,p+6,2)};
    return 1;
}
static Box intersect(Box a,Box b) {
    if(b.t>a.t)a.t=b.t;
    if(b.l>a.l)a.l=b.l;
    if(b.b<a.b)a.b=b.b;
    if(b.r<a.r)a.r=b.r;
    return a;
}
static int handle(MacRuntime *r,uint32_t h,size_t n,uint32_t *p) {
    if(!h || valid(r,h,4,1)<0)return fail(r,"Invalid color handle.");
    *p=rd(r,h,4);
    return *p?valid(r,*p,n,1):fail(r,"Empty color handle.");
}
static int region(MacRuntime *r,uint32_t h,Box *b) {
    uint32_t p;
    if(handle(r,h,10,&p)<0)return -1;
    if(rd(r,p,2)!=10)return fail(r,"Complex color clipping regions are unsupported.");
    return box(r,p+2,b);
}
static int pattern(MacRuntime *r,uint32_t h,uint8_t out[8]) {
    uint32_t p;
    if(handle(r,h,28,&p)<0)return -1;
    if(rd(r,p,2)!=0)return fail(r,"Complex color pixel patterns are unsupported.");
    memcpy(out,mac_cpu_ram(r->cpu)+p+20,8);return 1;
}
static int rgb(MacRuntime *rt,uint32_t port,uint32_t arg,int back) {
    if(!arg || valid(rt,arg,6,1)<0)return fail(rt,"Invalid RGBColor pointer.");
    unsigned rgb[3]={rd(rt,arg,2),rd(rt,arg+2,2),rd(rt,arg+4,2)};
    uint8_t allowed[256];memset(allowed,1,sizeof allowed);
    if(rt->main_device && mac_palette_match_mask(rt,allowed)<0)return -1;
    uint64_t best=UINT64_MAX;unsigned index=0;
    for(unsigned i=0;i<256;i++) {
        if(!allowed[i])continue;
        uint32_t c=rt->palette[i];uint64_t d=0;
        for(unsigned k=0;k<3;k++) {
            int64_t delta=(int64_t)rgb[k]-(int64_t)(((c>>(16-8*k))&255)*257);
            d+=(uint64_t)(delta*delta);
        }
        if(d<best){best=d;index=i;}
    }
    if(best==UINT64_MAX)return fail(rt,"No unreserved device color matches are available.");
    /* Snapshot RGB input before writing, permitting source/destination alias. */
    for(unsigned k=0;k<3;k++)wr(rt,port+(back?42:36)+k*2,2,rgb[k]);
    wr(rt,port+(back?84:80),4,index);return 1;
}
static int draw(MacRuntime *rt,uint32_t port,uint32_t sp,uint16_t op) {
    uint32_t pm;
    if(handle(rt,rd(rt,port+2,4),50,&pm)<0)return -1;
    unsigned row=rd(rt,pm+4,2);
    if((row&0xc000)!=0x8000 || rd(rt,pm+30,2)!=0 || rd(rt,pm+32,2)!=8 ||
       rd(rt,pm+34,2)!=1 || rd(rt,pm+36,2)!=8 || rd(rt,pm+16,2)!=0)
        return fail(rt,"Only unpacked indexed-8 color PixMaps are supported.");
    row&=0x3fff;
    Box bounds,clip,b,rg;
    if(box(rt,pm+6,&bounds)<0 || box(rt,port+16,&clip)<0)return -1;
    int width=bounds.r-bounds.l,height=bounds.b-bounds.t;
    uint32_t base=rd(rt,pm,4);
    if(width<0 || height<0 || (unsigned)width>row || (uint64_t)row*(unsigned)height>8u*1024u*1024u)
        return fail(rt,"Color PixMap dimensions exceed raster budget.");
    if(valid(rt,base,(size_t)row*(unsigned)height,0)<0)return -1;
    clip=intersect(clip,bounds);
    for(unsigned off=24;off<=28;off+=4) {
        if(region(rt,rd(rt,port+off,4),&rg)<0)return -1;
        clip=intersect(clip,rg);
    }
    uint32_t arg=rd(rt,sp,4);
    if(box(rt,op==0xa8a5?rd(rt,sp+4,4):arg,&b)<0)return -1;
    unsigned fg=rd(rt,port+80,4),bg=rd(rt,port+84,4);
    if(fg>255 || bg>255)return fail(rt,"Color indices exceed 8-bit palette.");
    int mode=(op==0xa8a1 || op==0xa8a2)?(int)rd(rt,port+56,2):8;
    if(mode<8 || mode>15)return fail(rt,"Unsupported color pattern transfer mode.");
    uint8_t pat[8];
    if(op==0xa8a5) {
        if(!arg || valid(rt,arg,8,0)<0)return fail(rt,"Invalid color fill pattern.");
        memcpy(pat,mac_cpu_ram(rt->cpu)+arg,8);
    } else if(op==0xa8a4) memset(pat,255,8);
    else if(pattern(rt,rd(rt,port+(op==0xa8a3?32:58),4),pat)<0)return -1;
    int ph=(int16_t)rd(rt,port+52,2),pw=(int16_t)rd(rt,port+54,2);
    if(op==0xa8a1 && (ph<0 || pw<0))return fail(rt,"Negative color pen size.");
    if(op==0xa8a1 && (!ph || !pw || (int16_t)rd(rt,port+66,2)<0))return 1;
    Box c=intersect(b,clip);
    /* Every guest range and unsupported representation is checked before writes. */
    for(int y=c.t;y<c.b;y++)for(int x=c.l;x<c.r;x++) {
        if(op==0xa8a1 && x>=b.l+pw && x<b.r-pw && y>=b.t+ph && y<b.b-ph)continue;
        uint8_t *p=mac_cpu_ram(rt->cpu)+base+(size_t)(y-bounds.t)*row+(unsigned)(x-bounds.l);
        int bit=!!(pat[(unsigned)y&7]&(0x80u>>((unsigned)x&7)));
        if(mode&4)bit=!bit;
        if(op==0xa8a4)*p^=255;
        else { switch(mode&3) {
            case 0:*p=(uint8_t)(bit?fg:bg);break;
            case 1: { if(bit)*p=(uint8_t)fg; break; }
            case 2: { if(bit)*p^=255; break; }
            case 3: { if(bit)*p=(uint8_t)bg; break; }
        } }
    }
    return 1;
}
int mac_color_raster_service(MacRuntime *rt,uint16_t opcode) {
    if((opcode<0xa8a1 || opcode>0xa8a5) && opcode!=0xaa14 && opcode!=0xaa15)return 0;
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
    if(!rt->qd_globals || valid(rt,rt->qd_globals,4,1)<0)return fail(rt,"QuickDraw globals are not initialized.");
    uint32_t port=rd(rt,rt->qd_globals,4);
    if(!port || valid(rt,port,108,1)<0)return fail(rt,"Invalid current color port.");
    if((rd(rt,port+6,2)&0xc000)!=0xc000)return 0;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);unsigned args=opcode==0xa8a5?8:4;
    if(valid(rt,sp,args,1)<0)return -1;
    int rc=opcode==0xaa14 || opcode==0xaa15?rgb(rt,port,rd(rt,sp,4),opcode==0xaa15):draw(rt,port,sp,opcode);
    if(rc>0)mac_cpu_set(rt->cpu,MAC_A7,sp+args);
    return rc;
}
