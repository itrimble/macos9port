#include "mac_runtime.h"
#include "mac_shape.h"

#include <stdint.h>
#include <string.h>

/* Independently implemented mono subset from published QuickDraw interfaces:
 * https://www.sysecol2.ethz.ch/RAMSES/Objects/AuxLibE/DMQuickDraw.html
 * Rectangular clipping only. Diagonal lines, PixMaps and text are unsupported.
 * CPU reference rasterizer, not hardware acceleration.
 */
typedef struct { int t, l, b, r; } Box;
typedef struct {
    MacRuntime *rt;
    uint32_t port, base;
    unsigned row;
    Box bounds, clip;
    uint8_t pattern[8];
    int mode;
} Raster;

static int fail(MacRuntime *rt, const char *message) {
    mac_cpu_halt(rt->cpu, message);
    return -1;
}

static int range(MacRuntime *rt, uint32_t p, size_t n, int aligned) {
    size_t size = mac_cpu_ram_size(rt->cpu);
    if ((aligned && (p & 1)) || p > size || n > size - p)
        return fail(rt, "QuickDraw raster address exceeds guest memory or is misaligned.");
    return 1;
}

static uint32_t rd(MacRuntime *rt, uint32_t p, unsigned n) { return mac_cpu_read(rt->cpu, p, n); }
static void wr(MacRuntime *rt, uint32_t p, unsigned n, uint32_t v) { mac_cpu_write(rt->cpu, p, n, v); }

static int get_box(MacRuntime *rt, uint32_t p, Box *b) {
    if (!p || range(rt, p, 8, 1) < 0) return -1;
    *b = (Box){(int16_t)rd(rt,p,2), (int16_t)rd(rt,p+2,2),
               (int16_t)rd(rt,p+4,2), (int16_t)rd(rt,p+6,2)};
    return 1;
}

static void put_box(MacRuntime *rt, uint32_t p, Box b) {
    wr(rt,p,2,(uint16_t)b.t); wr(rt,p+2,2,(uint16_t)b.l);
    wr(rt,p+4,2,(uint16_t)b.b); wr(rt,p+6,2,(uint16_t)b.r);
}

static Box intersect(Box a, Box b) {
    if (b.t > a.t) a.t=b.t;
    if (b.l > a.l) a.l=b.l;
    if (b.b < a.b) a.b=b.b;
    if (b.r < a.r) a.r=b.r;
    return a;
}

static int current_port(MacRuntime *rt, uint32_t *p) {
    if (!rt->qd_globals || range(rt, rt->qd_globals, 4, 1) < 0)
        return fail(rt, "QuickDraw globals are not initialized.");
    *p = rd(rt, rt->qd_globals, 4);
    if (!*p || range(rt, *p, 108, 1) < 0) return fail(rt, "QuickDraw has no valid current port.");
    rt->current_port = *p;
    return 1;
}

static int region_box(MacRuntime *rt, uint32_t h, uint32_t *p, Box *b) {
    if (!h || range(rt, h, 4, 1) < 0) return fail(rt, "QuickDraw region handle is invalid.");
    *p = rd(rt, h, 4);
    if (!*p || range(rt, *p, 10, 1) < 0) return fail(rt, "QuickDraw region data is invalid.");
    if (rd(rt, *p, 2) != 10) return fail(rt, "Complex QuickDraw regions are not supported yet.");
    return get_box(rt, *p + 2, b);
}

static int canvas(Raster *r, MacRuntime *rt, uint32_t port) {
    memset(r, 0, sizeof *r); r->rt=rt; r->port=port;
    unsigned row=rd(rt,port+6,2);
    if (row & 0xc000) return fail(rt, "Color PixMap rasterization is not supported yet.");
    r->row=row; r->base=rd(rt,port+2,4);
    if (rt->screen_depth != 1 && r->base == rt->screen_base)
        return fail(rt, "Monochrome drawing into the indexed screen is unsupported.");
    if (get_box(rt,port+8,&r->bounds)<0 || get_box(rt,port+16,&r->clip)<0) return -1;
    int width=r->bounds.r-r->bounds.l, height=r->bounds.b-r->bounds.t;
    if (width<0 || height<0 || (unsigned)width>row*8u ||
        (uint64_t)width*(unsigned)height>8u*1024u*1024u)
        return fail(rt, "QuickDraw bitmap dimensions exceed the mono raster budget.");
    if (range(rt,r->base,(size_t)row*(unsigned)height,0)<0) return -1;
    r->clip=intersect(r->clip,r->bounds);
    for (unsigned off=24;off<=28;off+=4) {
        uint32_t data; Box b;
        if (region_box(rt,rd(rt,port+off,4),&data,&b)<0) return -1;
        r->clip=intersect(r->clip,b);
    }
    if (rd(rt,port+80,4)!=33 || rd(rt,port+84,4)!=30)
        return fail(rt, "Non-default mono foreground/background colors are unsupported.");
    return 1;
}

static void pixel(Raster *r, int x, int y, int invert) {
    unsigned bx=(unsigned)(x-r->bounds.l), by=(unsigned)(y-r->bounds.t);
    uint8_t *p=mac_cpu_ram(r->rt->cpu)+r->base+(size_t)by*r->row+bx/8;
    uint8_t mask=(uint8_t)(0x80u>>(bx&7));
    int old=!!(*p&mask), bit=!!(r->pattern[(unsigned)y&7]&(0x80u>>((unsigned)x&7)));
    if (r->mode&4) bit=!bit;
    int value=old;
    if (invert) value=!old;
    else switch(r->mode&3) {
        case 0:value=bit;break;
        case 1:value=bit|old;break;
        case 2:value=bit^old;break;
        case 3:value=old&!bit;break;
    }
    if (value) *p|=mask; else *p&=(uint8_t)~mask;
}

static int paint(MacRuntime *rt,uint32_t port,Box box,uint32_t pattern,int mode,
                 int invert,int frame,const MacShape *outer,const MacShape *inner) {
    Raster r;
    if (mode<8 || mode>15) return fail(rt,"Unsupported QuickDraw pattern transfer mode.");
    if (range(rt,pattern,8,0)<0 || canvas(&r,rt,port)<0) return -1;
    memcpy(r.pattern,mac_cpu_ram(rt->cpu)+pattern,8); r.mode=mode-8;
    int ph=(int16_t)rd(rt,port+52,2), pw=(int16_t)rd(rt,port+54,2);
    if (frame && (ph<0 || pw<0)) return fail(rt,"Negative QuickDraw pen size.");
    if (frame && (!ph || !pw || (int16_t)rd(rt,port+66,2)<0)) return 1;
    Box clipped=intersect(box,r.clip);
    for (int y=clipped.t;y<clipped.b;y++) for(int x=clipped.l;x<clipped.r;x++) {
        if (outer && !mac_shape_contains(outer,x,y)) continue;                 /* an oval or round rectangle: only its rows */
        if (frame && (inner ? mac_shape_contains(inner,x,y) : (x>=box.l+pw && x<box.r-pw && y>=box.t+ph && y<box.b-ph))) continue;
        pixel(&r,x,y,invert);
    }
    return 1;
}

/* An oval or round rectangle on a black-and-white port, with the same pattern, mode and clip rules as the rectangle calls (mac_shapes.c). */
int mac_raster_shape(MacRuntime *rt,uint32_t port,int t,int l,int b,int r,uint32_t pattern,int mode,int invert,int frame,const MacShape *outer,const MacShape *inner) {
    return paint(rt,port,(Box){t,l,b,r},pattern,mode,invert,frame,outer,inner);
}
int mac_raster_service(MacRuntime *rt,uint16_t opcode) {
    unsigned args;
    switch(opcode) {
        case 0xa87c: case 0xa879: case 0xa87a: case 0xa87b:
        case 0xa893: case 0xa891: case 0xa89b: case 0xa89d: case 0xa89a:
        case 0xa8a1: case 0xa8a2: case 0xa8a3: case 0xa8a4: args=4;break;
        case 0xa8a5: args=8;break;
        case 0xa89c: args=2;break;
        case 0xa896: case 0xa897: case 0xa89e: args=0;break;
        default:return 0;
    }
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7),port;
    if (range(rt,sp,args,1)<0 || current_port(rt,&port)<0) return -1;
    /* GetClip, SetClip and ClipRect only move the clip region, and clipRgn sits at port+28 in both a GrafPort and a
     * CGrafPort, so they work on either. Everything else here reads pen or pattern fields whose offsets differ. */
    int clip_only=opcode==0xa879 || opcode==0xa87a || opcode==0xa87b;
    if (!clip_only && (rd(rt,port+6,2)&0xc000)==0xc000)
        return fail(rt,"This pen or drawing operation is not implemented for color ports yet.");
    uint32_t arg=args>=4?rd(rt,sp,4):0;
    int rc=1;
    if (opcode>=0xa8a1 && opcode<=0xa8a5) {
        uint32_t rectptr=opcode==0xa8a5?rd(rt,sp+4,4):arg;
        uint32_t pat=opcode==0xa8a5?arg:port+(opcode==0xa8a3?32:58);
        int mode=(opcode==0xa8a1 || opcode==0xa8a2)?(int)rd(rt,port+56,2):8;
        Box b;
        if(get_box(rt,rectptr,&b)<0)return -1;
        rc=paint(rt,port,b,pat,mode,opcode==0xa8a4,opcode==0xa8a1,NULL,NULL);
    } else if(opcode==0xa879 || opcode==0xa87a || opcode==0xa87b) {
        uint32_t dest; Box b;
        if(opcode==0xa87b) { if(get_box(rt,arg,&b)<0)return -1; }
        else { uint32_t src; if(region_box(rt,opcode==0xa879?arg:rd(rt,port+28,4),&src,&b)<0)return -1; }
        Box ignored;
        if(region_box(rt,opcode==0xa87a?arg:rd(rt,port+28,4),&dest,&ignored)<0)return -1;
        put_box(rt,dest+2,b);
    } else if(opcode==0xa893 || opcode==0xa891) {
        int y=(int16_t)rd(rt,sp,2),x=(int16_t)rd(rt,sp+2,2);
        if(opcode==0xa891) {
            int oldy=(int16_t)rd(rt,port+48,2),oldx=(int16_t)rd(rt,port+50,2);
            if(oldx!=x && oldy!=y)return fail(rt,"Diagonal QuickDraw lines are not supported yet.");
            int ph=(int16_t)rd(rt,port+52,2),pw=(int16_t)rd(rt,port+54,2);
            if(ph<0 || pw<0)return fail(rt,"Negative QuickDraw pen size.");
            Box b={oldy<y?oldy:y,oldx<x?oldx:x,(oldy>y?oldy:y)+ph,(oldx>x?oldx:x)+pw};
            if(ph && pw && (int16_t)rd(rt,port+66,2)>=0)
                rc=paint(rt,port,b,port+58,(int)rd(rt,port+56,2),0,0,NULL,NULL);
        }
        if(rc>0) {wr(rt,port+48,2,(uint16_t)y);wr(rt,port+50,2,(uint16_t)x);}
    } else if(opcode==0xa89b) {
        if((int16_t)rd(rt,sp,2)<0 || (int16_t)rd(rt,sp+2,2)<0)return fail(rt,"Negative QuickDraw pen size.");
        wr(rt,port+52,4,arg);
    } else if(opcode==0xa89c) {
        uint32_t mode=rd(rt,sp,2);
        if(mode<8 || mode>15)return fail(rt,"Unsupported QuickDraw pattern transfer mode.");
        wr(rt,port+56,2,mode);
    } else if(opcode==0xa89d || opcode==0xa87c) {
        if(!arg || range(rt,arg,8,0)<0)return -1;
        memmove(mac_cpu_ram(rt->cpu)+port+(opcode==0xa87c?32:58),mac_cpu_ram(rt->cpu)+arg,8);
    } else if(opcode==0xa89a) {
        if(!arg || range(rt,arg,4,1)<0)return -1;
        wr(rt,arg,4,rd(rt,port+48,4));
    } else if(opcode==0xa89e) {
        wr(rt,port+52,4,0x00010001);wr(rt,port+56,2,8);
        memset(mac_cpu_ram(rt->cpu)+port+58,255,8);
    } else {
        uint16_t vis=(uint16_t)rd(rt,port+66,2);
        wr(rt,port+66,2,(uint16_t)(vis+(opcode==0xa896?-1:1)));
    }
    if(rc>0 && !mac_cpu_halted(rt->cpu))mac_cpu_set(rt->cpu,MAC_A7,sp+args);
    return mac_cpu_halted(rt->cpu)?-1:rc;
}
