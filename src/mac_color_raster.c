#include "mac_runtime.h"
#include "mac_qdcolor.h"
#include "mac_shape.h"
#include "mac_cicn.h"
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
static int set_rgb(MacRuntime *rt,uint32_t port,const unsigned rgb[3],int back);
static int rgb(MacRuntime *rt,uint32_t port,uint32_t arg,int back) {
    if(!arg || valid(rt,arg,6,1)<0)return fail(rt,"Invalid RGBColor pointer.");
    unsigned in[3]={rd(rt,arg,2),rd(rt,arg+2,2),rd(rt,arg+4,2)};
    return set_rgb(rt,port,in,back);
}
/* ForeColor / BackColor(color: LONGINT) on a colour port: one of QuickDraw's eight constants (mac_qdcolor.h) becomes the RGB the port uses. */
static int constant_color(MacRuntime *rt,uint32_t port,uint32_t color,int back) {
    unsigned c8[3];
    if(!mac_qd_color_constant(color,c8))return fail(rt,"ForeColor/BackColor take one of QuickDraw's eight color constants here.");
    unsigned in[3]={c8[0]*257,c8[1]*257,c8[2]*257};
    return set_rgb(rt,port,in,back);
}
static int set_rgb(MacRuntime *rt,uint32_t port,const unsigned rgb[3],int back) {
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
/* preset: when not NULL the rectangle to paint (the call is PaintRect, op $A8A2), instead of one read from the stack. */
static int draw(MacRuntime *rt,uint32_t port,uint32_t sp,uint16_t op,const Box *preset,uint32_t fillpat,const MacShape *outer,const MacShape *inner,const MacPixTile *tile) {
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
    uint32_t arg=preset?fillpat:rd(rt,sp,4);
    if(preset)b=*preset;
    else if(box(rt,op==0xa8a5?rd(rt,sp+4,4):arg,&b)<0)return -1;
    unsigned fg=rd(rt,port+80,4),bg=rd(rt,port+84,4);
    if(fg>255 || bg>255)return fail(rt,"Color indices exceed 8-bit palette.");
    int mode=(op==0xa8a1 || op==0xa8a2)?(int)rd(rt,port+56,2):8;
    /* 44 is transparent + patCopy (Apple's Color QuickDraw chapter: "To specify a transparent pattern, add the transparent constant to the patCopy
     * constant"): the pattern's set bits paint the foreground, and its clear bits, which are the background colour, leave the destination alone. The
     * arithmetic transparent mode replaces the destination with the source unless the source is the background colour, so with a foreground
     * equal to the background nothing is painted at all. */
    int transparent=mode==44;
    if(transparent)mode=8;
    /* A source mode used as a pen mode acts as the pattern mode of the same operation: Apple's table of Boolean transfer modes (Inside Macintosh,
     * QuickDraw) gives patCopy/srcCopy, patOr/srcOr, patXor/srcXor, patBic/srcBic and the notPat/notSrc forms identical actions. */
    if(mode<8)mode+=8;
    if(mode<8 || mode>15)return fail(rt,"Unsupported color pattern transfer mode.");
    uint8_t pat[8];
    if(tile) memset(pat,255,8);
    else if(op==0xa8a5) {
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
        if(outer && !mac_shape_contains(outer,x,y))continue;                   /* an oval or round rectangle: only its rows */
        if(op==0xa8a1 && (inner?mac_shape_contains(inner,x,y):(x>=b.l+pw && x<b.r-pw && y>=b.t+ph && y<b.b-ph)))continue;
        uint8_t *p=mac_cpu_ram(rt->cpu)+base+(size_t)(y-bounds.t)*row+(unsigned)(x-bounds.l);
        if(tile){*p=tile->px[(size_t)(((y%tile->h)+tile->h)%tile->h)*(size_t)tile->w+(size_t)(((x%tile->w)+tile->w)%tile->w)];continue;}
        int bit=!!(pat[(unsigned)y&7]&(0x80u>>((unsigned)x&7)));
        if(mode&4)bit=!bit;
        if(op==0xa8a4)*p^=255;
        else if(transparent){ if(bit && fg!=bg)*p=(uint8_t)fg; }
        else { switch(mode&3) {
            case 0:*p=(uint8_t)(bit?fg:bg);break;
            case 1: { if(bit)*p=(uint8_t)fg; break; }
            case 2: { if(bit)*p^=255; break; }
            case 3: { if(bit)*p=(uint8_t)bg; break; }
        } }
    }
    return 1;
}
/* Paint a rectangle in the port's pen pattern and mode, clipped like PaintRect (used for pen strokes). */
int mac_color_paint_box(MacRuntime *rt,uint32_t port,int t,int l,int b,int r) {
    Box x={t,l,b,r};
    return draw(rt,port,0,0xa8a2,&x,0,NULL,NULL,NULL);
}
/* An oval or round rectangle on an indexed colour port: `op` is the rectangle call it behaves like ($A8A1 frame, $A8A2 paint, $A8A3 erase, $A8A4 invert,
 * $A8A5 fill with the pattern at fillpat). */
int mac_color_shape(MacRuntime *rt,uint32_t port,int t,int l,int b,int r,uint16_t op,uint32_t fillpat,const MacShape *outer,const MacShape *inner) {
    Box x={t,l,b,r};
    return draw(rt,port,0,op,&x,fillpat,outer,inner,NULL);
}
/* The same, filled with the pixels of a pixel pattern (mac_pixpat.c) instead of a 1-bit pattern: a plain copy, tiled from the port's local origin. */
int mac_color_tile(MacRuntime *rt,uint32_t port,int t,int l,int b,int r,const MacPixTile *tile,const MacShape *outer) {
    Box x={t,l,b,r};
    return draw(rt,port,0,0xa8a5,&x,0,outer,NULL,tile);
}
/* GetForeColor / GetBackColor(VAR color: RGBColor) ($AA19 / $AA1A, MPW listing): a colour port holds the RGB it was given (CGrafPort +36 / +42);
 * a basic port holds one of QuickDraw's eight colour constants, reported as the pure colour it stands for. Anything else halts. */
static int get_color(MacRuntime *rt,uint32_t port,uint32_t out,int back) {
    if(!out || valid(rt,out,6,1)<0)return fail(rt,"Invalid RGBColor pointer.");
    unsigned c[3];
    if((rd(rt,port+6,2)&0xc000)==0xc000){for(unsigned k=0;k<3;k++)c[k]=rd(rt,port+(back?42:36)+k*2,2);}
    else {
        unsigned c8[3];
        if(!mac_qd_color_constant(rd(rt,port+(back?84:80),4),c8))return fail(rt,"GetForeColor/GetBackColor on a basic port with a color other than QuickDraw's eight constants is unsupported.");
        for(unsigned k=0;k<3;k++)c[k]=c8[k]*257;
    }
    for(unsigned k=0;k<3;k++)wr(rt,out+k*2,2,c[k]);
    return 1;
}
int mac_color_raster_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode==0xaa19 || opcode==0xaa1a) {
        if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
        if(!rt->qd_globals || valid(rt,rt->qd_globals,4,1)<0)return fail(rt,"QuickDraw globals are not initialized.");
        uint32_t port=rd(rt,rt->qd_globals,4),sp=mac_cpu_get(rt->cpu,MAC_A7);
        if(!port || valid(rt,port,108,1)<0 || valid(rt,sp,4,1)<0)return fail(rt,"Invalid current port.");
        int rc=get_color(rt,port,rd(rt,sp,4),opcode==0xaa1a);
        if(rc>0)mac_cpu_set(rt->cpu,MAC_A7,sp+4);
        return rc;
    }
    if((opcode<0xa8a1 || opcode>0xa8a5) && opcode!=0xaa14 && opcode!=0xaa15 && opcode!=0xa862 && opcode!=0xa863)return 0;
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
    if(!rt->qd_globals || valid(rt,rt->qd_globals,4,1)<0)return fail(rt,"QuickDraw globals are not initialized.");
    uint32_t port=rd(rt,rt->qd_globals,4);
    if(!port || valid(rt,port,108,1)<0)return fail(rt,"Invalid current color port.");
    if((rd(rt,port+6,2)&0xc000)!=0xc000)return 0;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);unsigned args=opcode==0xa8a5?8:4;
    if(valid(rt,sp,args,1)<0)return -1;
    int rc=opcode==0xa862 || opcode==0xa863?constant_color(rt,port,rd(rt,sp,4),opcode==0xa863):opcode==0xaa14 || opcode==0xaa15?rgb(rt,port,rd(rt,sp,4),opcode==0xaa15):draw(rt,port,sp,opcode,NULL,0,NULL,NULL,NULL);
    if(rc>0)mac_cpu_set(rt->cpu,MAC_A7,sp+args);
    return rc;
}

int mac_color_target(MacRuntime *rt,uint32_t port,MacColorTarget *out) {
    if(!port)return fail(rt,"There is no current graphics port.");
    if(valid(rt,port,108,0)<0)return -1;
    if((rd(rt,port+6,2)&0xc000)!=0xc000)return 0;
    uint32_t pm;
    if(handle(rt,rd(rt,port+2,4),50,&pm)<0)return -1;
    unsigned row=rd(rt,pm+4,2);
    if((row&0xc000)!=0x8000 || rd(rt,pm+30,2)!=0 || rd(rt,pm+32,2)!=8 || rd(rt,pm+34,2)!=1 || rd(rt,pm+36,2)!=8 || rd(rt,pm+16,2)!=0)
        return fail(rt,"Only unpacked indexed-8 color PixMaps are supported.");
    row&=0x3fff;
    Box bounds,clip,rg;
    if(box(rt,pm+6,&bounds)<0 || box(rt,port+16,&clip)<0)return -1;
    int width=bounds.r-bounds.l,height=bounds.b-bounds.t;
    uint32_t base=rd(rt,pm,4);
    if(width<0 || height<0 || (unsigned)width>row || (uint64_t)row*(unsigned)height>8u*1024u*1024u)return fail(rt,"Color PixMap dimensions exceed raster budget.");
    if(valid(rt,base,(size_t)row*(unsigned)height,0)<0)return -1;
    clip=intersect(clip,bounds);
    for(unsigned off=24;off<=28;off+=4) {
        if(region(rt,rd(rt,port+off,4),&rg)<0)return -1;
        clip=intersect(clip,rg);
    }
    out->pixels=mac_cpu_ram(rt->cpu)+base;out->row=(int)row;
    out->bt=bounds.t;out->bl=bounds.l;out->bb=bounds.b;out->br=bounds.r;
    out->ct=clip.t;out->cl=clip.l;out->cb=clip.b;out->cr=clip.r;
    return 1;
}
