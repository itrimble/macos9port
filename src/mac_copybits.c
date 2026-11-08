#include "mac_runtime.h"
#include "mac_rgn.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Independent bounded CPU CopyBits; public ABI/semantics:
 * dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-166.html */
typedef struct { int t,l,b,r; } Box;
typedef struct { uint32_t base,table; unsigned row,depth; Box bounds; } Image;
static int fail(MacRuntime *rt,const char*s){mac_cpu_halt(rt->cpu,s);return -1;}
static int valid(MacRuntime *rt,uint32_t p,size_t n,int align){
    if((align&&(p&1))||p>mac_cpu_ram_size(rt->cpu)||n>mac_cpu_ram_size(rt->cpu)-p)
        return fail(rt,"CopyBits range exceeds guest RAM or is unaligned.");
    return 1;
}
static uint32_t rd(MacRuntime*rt,uint32_t p,unsigned n){return mac_cpu_read(rt->cpu,p,n);}
static int box(MacRuntime*rt,uint32_t p,Box*b){
    if(!p)return fail(rt,"CopyBits received NIL rectangle.");
    if(valid(rt,p,8,1)<0)return -1;
    *b=(Box){(int16_t)rd(rt,p,2),(int16_t)rd(rt,p+2,2),(int16_t)rd(rt,p+4,2),(int16_t)rd(rt,p+6,2)};return 1;
}
static Box intersect(Box a,Box b){
    if(b.t>a.t)a.t=b.t;if(b.l>a.l)a.l=b.l;
    if(b.b<a.b)a.b=b.b;if(b.r<a.r)a.r=b.r;return a;
}
/* Complex mask, visible and clip regions: the regions kept for the transfer in progress (slot 0 the mask, 1 visRgn, 2 clipRgn), tested per pixel;
 * a plain rectangle region only narrows the box. Freed by the service entry points. */
static MacRgn *g_rgn[3];
static void free_rgns(void){for(unsigned i=0;i<3;i++){if(g_rgn[i]){mac_rgn_free(g_rgn[i]);free(g_rgn[i]);g_rgn[i]=NULL;}}}
static int inside(int x,int y){for(unsigned i=0;i<3;i++)if(g_rgn[i]&&!mac_rgn_contains(g_rgn[i],x,y))return 0;return 1;}
static int region(MacRuntime*rt,uint32_t h,Box*b,unsigned slot){
    if(!h||valid(rt,h,4,1)<0)return fail(rt,"CopyBits invalid region handle.");
    uint32_t p=rd(rt,h,4);
    if(!p||valid(rt,p,10,1)<0)return fail(rt,"CopyBits invalid region data.");
    if(rd(rt,p,2)!=10) {
        MacRgn *g=malloc(sizeof *g);
        if(!g)return fail(rt,"CopyBits could not allocate a region.");
        mac_rgn_init(g);
        int rc=mac_region_load_copy(rt,h,(void*)g);
        if(rc<=0){mac_rgn_free(g);free(g);return rc<0?-1:fail(rt,"CopyBits complex region is not a valid region handle.");}
        MacRect bb;mac_rgn_bbox(g,&bb);*b=(Box){bb.t,bb.l,bb.b,bb.r};
        if(g_rgn[slot]){mac_rgn_free(g_rgn[slot]);free(g_rgn[slot]);}
        g_rgn[slot]=g;return 1;
    }
    return box(rt,p+2,b);
}
static int image(MacRuntime*rt,uint32_t p,Image*i){
    if(!p||valid(rt,p,14,1)<0)return fail(rt,"CopyBits invalid bitmap.");
    unsigned row=rd(rt,p+4,2);
    if((row&0xc000)==0xc000) {
        /* The address of a colour port's portBits field (port+2): its "rowBytes" word is the port's portVersion ($C000), which marks the
         * four bytes at p as the handle of the port's PixMap rather than a base address. Use that PixMap. */
        uint32_t h=rd(rt,p,4);
        if(!h||valid(rt,h,4,1)<0)return fail(rt,"CopyBits colour port has no valid PixMap handle.");
        p=rd(rt,h,4);
        if(!p||valid(rt,p,50,1)<0)return fail(rt,"CopyBits colour port PixMap is invalid.");
        row=rd(rt,p+4,2);
        if((row&0xc000)==0xc000)return fail(rt,"CopyBits colour port PixMap is itself a port.");
    }i->base=rd(rt,p,4);i->row=row&0x3fff;i->depth=1;i->table=0;
    if(box(rt,p+6,&i->bounds)<0)return -1;
    if(row&0x8000){
        if(valid(rt,p,50,1)<0)return -1;
        i->depth=rd(rt,p+32,2);i->table=rd(rt,p+42,4);
        if(rd(rt,p+16,2)||rd(rt,p+30,2)||rd(rt,p+34,2)!=1||rd(rt,p+36,2)!=i->depth)
            return fail(rt,"CopyBits packed or direct pixel format unsupported.");
    }
    if((row&0x4000)||(i->depth!=1&&i->depth!=8))return fail(rt,"CopyBits supports 1-bit and 8-bit images only.");
    int w=i->bounds.r-i->bounds.l,h=i->bounds.b-i->bounds.t;
    if(w<0||h<0||(uint64_t)w*i->depth>(uint64_t)i->row*8||
       (uint64_t)w*(unsigned)h>8u*1024*1024)return fail(rt,"CopyBits image exceeds pixel budget.");
    return valid(rt,i->base,(size_t)i->row*(unsigned)h,0);
}
/* A color table. Bit 15 of ctFlags (device table: the value fields are ignored) is always fine. Bit 14 marks a palette-linked
 * table: CopyBits reads each entry's value as an index into the palette attached to the destination window and ignores the
 * rgb field (Technical Note QD13, which cites Inside Macintosh VI, "Palette Manager"). That meaning is implemented for a
 * source table only (`linked` is set); on a destination, or with any reserved bit, the table is refused. */
static int table_header(MacRuntime*rt,uint32_t h,uint32_t*p,int source,int*linked){
    if(!h||valid(rt,h,4,1)<0)return fail(rt,"CopyBits missing indexed color table.");
    *p=rd(rt,h,4);
    if(!*p||valid(rt,*p,8,1)<0)return fail(rt,"CopyBits invalid color table header.");
    unsigned flags=rd(rt,*p+4,2);
    if(flags&0x3fff)return fail(rt,"CopyBits custom or reserved color table flags unsupported.");
    *linked=(flags&0x4000)!=0;
    if(*linked && !source)return fail(rt,"CopyBits palette-linked destination color table unsupported.");
    return 1;
}
/* A source rectangle that reaches past the source bitmap's bounds is cut back to them, and the destination rectangle shrinks in proportion, so only
 * pixels the source really has are transferred (the outside part is not copied). Nothing left means an empty destination. */
static void crop_to_source(Box*sr,Box*dr,Box bounds){
    Box o=*sr,d=*dr;int64_t sw=o.r-o.l,sh=o.b-o.t,dw=d.r-d.l,dh=d.b-d.t;
    if(sw<=0||sh<=0||dw<=0||dh<=0)return;
    Box c=intersect(o,bounds);
    if(c.l>=c.r||c.t>=c.b){*dr=(Box){d.t,d.l,d.t,d.l};*sr=(Box){o.t,o.l,o.t,o.l};return;}
    dr->l=d.l+(int)((c.l-o.l)*dw/sw);dr->r=d.l+(int)((c.r-o.l)*dw/sw);
    dr->t=d.t+(int)((c.t-o.t)*dh/sh);dr->b=d.t+(int)((c.b-o.t)*dh/sh);
    *sr=c;
}
static unsigned pixel(const unsigned char*ram,Image*i,int x,int y){
    unsigned dx=(unsigned)(x-i->bounds.l);uint32_t p=i->base+(unsigned)(y-i->bounds.t)*i->row;
    return i->depth==8?ram[p+dx]:(ram[p+dx/8]>>(7-dx%8))&1;
}
static void put(unsigned char*ram,Image*i,int x,int y,unsigned v){
    unsigned dx=(unsigned)(x-i->bounds.l);uint32_t p=i->base+(unsigned)(y-i->bounds.t)*i->row;
    if(i->depth==8)ram[p+dx]=(unsigned char)v;
    else {unsigned bit=1u<<(7-dx%8);ram[p+dx/8]=(unsigned char)((ram[p+dx/8]&~bit)|(v?bit:0));}
}
static int tables_ok(MacRuntime*rt,Image*srcp,Image*dstp,unsigned mode,int*linked_source,unsigned*palette_count,unsigned*table_size,unsigned char conv[256],int*convert){
    *convert=0;
    Image src=*srcp,dst=*dstp;
    if(src.depth==8){
        if(rt->trace)for(unsigned n=0;n<2;n++) {
            uint32_t h=n?dst.table:src.table;
            if(h && h<=mac_cpu_ram_size(rt->cpu)-4) {
                uint32_t p=rd(rt,h,4);
                if(p && p<=mac_cpu_ram_size(rt->cpu)-8)
                    fprintf(stderr,"CopyBits %s clut=%08x seed=%08x flags=%04x size=%04x mode=%u\n",n?"dst":"src",h,rd(rt,p,4),rd(rt,p+4,2),rd(rt,p+6,2),mode);
            }
        }

        uint32_t a,b;int linked=0,unused=0;
        if(table_header(rt,src.table,&a,1,&linked)<0||table_header(rt,dst.table,&b,0,&unused)<0)return -1;
        *linked_source=linked;
        if(linked) { /* each source pixel selects an entry whose value indexes the active window palette; see below */
            if(mode!=0)return fail(rt,"CopyBits palette-linked source supports srcCopy only.");
            if(!rt->palette_active||valid(rt,rt->palette_active,4,1)<0||valid(rt,rd(rt,rt->palette_active,4),2,1)<0)
                return fail(rt,"CopyBits palette-linked source needs an activated window palette.");
            *palette_count=rd(rt,rd(rt,rt->palette_active,4),2);*table_size=rd(rt,a+6,2);
            if(valid(rt,a,8+((size_t)*table_size+1)*8,1)<0)return fail(rt,"CopyBits palette-linked color table is truncated.");
        }
        /* QD13, ctSeed contract: srcCopy with equal source/destination seeds
         * transfers pixel values without consulting RGB entries. An empty
         * source table is therefore usable here; differing seeds still require
         * complete tables and verified color equivalence.
         * https://leopard-adc.pepas.com/technotes/qd/qd_13.html */
        if(!linked && (mode!=0 || rd(rt,a,4)!=rd(rt,b,4))) {
            if(rd(rt,a+6,2)!=255 || rd(rt,b+6,2)!=255 ||
               valid(rt,a,2056,1)<0 || valid(rt,b,2056,1)<0)
                return fail(rt,"CopyBits unequal-seed or non-srcCopy transfer requires 256-entry color tables.");
            for(unsigned j=0;j<256;j++)if(memcmp(mac_cpu_ram(rt->cpu)+a+10+j*8,mac_cpu_ram(rt->cpu)+b+10+j*8,6)){*convert=1;break;}
            if(*convert) {
                /* Different colour tables: each source pixel becomes the destination entry of the same colour, or the nearest by squared
                 * RGB distance (lowest index on a tie). QuickDraw maps through an inverse table of limited resolution, so this can pick a
                 * different entry for colours that are not in the destination table. Only srcCopy converts. */
                if(mode!=0)return fail(rt,"CopyBits color table conversion supports srcCopy only.");
                const unsigned char*ram=mac_cpu_ram(rt->cpu);
                for(unsigned j=0;j<256;j++) {
                    const unsigned char*c=ram+a+10+j*8;
                    uint64_t best=UINT64_MAX;unsigned pick=0;
                    for(unsigned k=0;k<256;k++) {
                        const unsigned char*d=ram+b+10+k*8;uint64_t sum=0;
                        for(unsigned q=0;q<3;q++){int x=(c[2*q]<<8|c[2*q+1])-(d[2*q]<<8|d[2*q+1]);sum+=(uint64_t)((int64_t)x*x);}
                        if(sum<best){best=sum;pick=k;}
                    }
                    conv[j]=(unsigned char)pick;
                }
            }
        }
    }
    return 1;
}
static int port_clip(MacRuntime*rt,Image*dstp,Box*clipp){
    Image dst=*dstp;Box clip=*clipp,r;
    uint32_t port=rt->current_port;
    if(rt->qd_globals){if(valid(rt,rt->qd_globals,4,1)<0)return -1;port=rd(rt,rt->qd_globals,4);}
    if(port){
        if(valid(rt,port,108,1)<0)return -1;
        uint32_t bm=port+2;
        int color=(rd(rt,port+6,2)&0xc000)==0xc000;
        if(color){uint32_t h=rd(rt,port+2,4);if(!h||valid(rt,h,4,1)<0)return fail(rt,"CopyBits invalid port pixmap.");bm=rd(rt,h,4);}
        if(valid(rt,bm,14,1)<0)return -1;
        if(rd(rt,bm,4)==dst.base){
            for(unsigned off=24;off<=28;off+=4){if(region(rt,rd(rt,port+off,4),&r,off==24?1:2)<0)return -1;clip=intersect(clip,r);}
        }
        if(color){
            if(rd(rt,port+36,4)||rd(rt,port+40,2)||rd(rt,port+42,4)!=0xffffffffu||rd(rt,port+46,2)!=0xffff)
                return fail(rt,"CopyBits nondefault port color mapping unsupported.");
        }else if(rd(rt,port+80,4)!=33||rd(rt,port+84,4)!=30)
            return fail(rt,"CopyBits nondefault mono colors unsupported.");
    }
    *clipp=clip;return 1;
}
/* CopyMask(srcBits, maskBits, dstBits; srcRect, maskRect, dstRect) ($A817): srcCopy where the 1-bit mask map is set, the destination left alone
 * where it is clear. Stack: dstRect at SP, then maskRect, srcRect, dstBits, maskBits, srcBits (24 bytes). The three rectangles are the same
 * size or scaled like CopyBits' (each destination pixel samples the source and the mask at the proportional position). The mask must be a
 * 1-bit map; source and destination must share a depth (1 or 8) and, at 8 bits, colour tables handled as srcCopy does. The mask rectangle
 * must lie inside the mask's bounds, and clipping is the destination port's, as for CopyBits. */
static int copymask(MacRuntime*rt){
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(valid(rt,sp,24,1)<0)return -1;
    Image src,dst,mask;Box sr,mr,dr;
    if(image(rt,rd(rt,sp+20,4),&src)<0||image(rt,rd(rt,sp+12,4),&dst)<0||image(rt,rd(rt,sp+16,4),&mask)<0||
       box(rt,rd(rt,sp+8,4),&sr)<0||box(rt,rd(rt,sp+4,4),&mr)<0||box(rt,rd(rt,sp,4),&dr)<0)return -1;
    if(mask.depth!=1)return fail(rt,"CopyMask needs a 1-bit mask bitmap.");
    if(src.depth!=dst.depth)return fail(rt,"CopyMask mixed pixel depths unsupported.");
    crop_to_source(&sr,&dr,src.bounds);
    unsigned palette_count=0,table_size=0;int linked_source=0;
    unsigned char conv[256];int convert=0;
    if(tables_ok(rt,&src,&dst,0,&linked_source,&palette_count,&table_size,conv,&convert)<0)return -1;
    if(linked_source)return fail(rt,"CopyMask with a palette-linked source color table is unsupported.");
    Box clip=intersect(dr,dst.bounds);
    if(port_clip(rt,&dst,&clip)<0)return -1;
    int sw=sr.r-sr.l,sh=sr.b-sr.t,dw=dr.r-dr.l,dh=dr.b-dr.t,mw=mr.r-mr.l,mh=mr.b-mr.t;
    if(sw<=0||sh<=0||dw<=0||dh<=0||mw<=0||mh<=0||clip.r<=clip.l||clip.b<=clip.t){mac_cpu_set(rt->cpu,MAC_A7,sp+24);return 1;}
    if(sr.l<src.bounds.l||sr.t<src.bounds.t||sr.r>src.bounds.r||sr.b>src.bounds.b)return fail(rt,"CopyMask source rectangle exceeds source bounds.");
    if(mr.l<mask.bounds.l||mr.t<mask.bounds.t||mr.r>mask.bounds.r||mr.b>mask.bounds.b)return fail(rt,"CopyMask mask rectangle exceeds mask bounds.");
    size_t w=(unsigned)(clip.r-clip.l),h=(unsigned)(clip.b-clip.t);
    if(w*h>8u*1024*1024)return fail(rt,"CopyMask transfer exceeds pixel budget.");
    unsigned char*snap=malloc(w*h*2);
    if(!snap)return fail(rt,"CopyMask snapshot allocation failed.");
    unsigned char*ram=mac_cpu_ram(rt->cpu);
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++){          /* snapshot first: the destination may overlap the source or the mask */
        int64_t fx=(int64_t)clip.l+(int)x-dr.l,fy=(int64_t)clip.t+(int)y-dr.t;
        {unsigned v=pixel(ram,&src,sr.l+(int)(fx*sw/dw),sr.t+(int)(fy*sh/dh));snap[2*(y*w+x)]=(unsigned char)(convert?conv[v&255]:v);}
        snap[2*(y*w+x)+1]=(unsigned char)pixel(ram,&mask,mr.l+(int)(fx*mw/dw),mr.t+(int)(fy*mh/dh));
    }
    unsigned bits=dst.depth==8?255:1;
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++)
        if(snap[2*(y*w+x)+1]&&inside(clip.l+(int)x,clip.t+(int)y))put(ram,&dst,clip.l+(int)x,clip.t+(int)y,snap[2*(y*w+x)]&bits);
    free(snap);mac_cpu_set(rt->cpu,MAC_A7,sp+24);return 1;
}
static int copybits_core(MacRuntime*rt,uint16_t opcode){
    if(opcode==0xa817){if(mac_cpu_halted(rt->cpu))return -1;return copymask(rt);}
    if(opcode!=0xa8ec)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(valid(rt,sp,22,1)<0)return -1;
    unsigned mode=rd(rt,sp+4,2);
    if(mode>7)return fail(rt,"CopyBits transfer mode unsupported.");
    Image src,dst;Box sr,dr;
    if(image(rt,rd(rt,sp+18,4),&src)<0||image(rt,rd(rt,sp+14,4),&dst)<0||
       box(rt,rd(rt,sp+10,4),&sr)<0||box(rt,rd(rt,sp+6,4),&dr)<0)return -1;
    if(src.depth!=dst.depth)return fail(rt,"CopyBits mixed pixel depths unsupported.");
    crop_to_source(&sr,&dr,src.bounds);
    unsigned palette_count=0,table_size=0;int linked_source=0;
    unsigned char conv[256];int convert=0;
    if(tables_ok(rt,&src,&dst,mode,&linked_source,&palette_count,&table_size,conv,&convert)<0)return -1;
    Box clip=intersect(dr,dst.bounds),r;
    uint32_t mask=rd(rt,sp,4);
    if(mask){if(region(rt,mask,&r,0)<0)return -1;clip=intersect(clip,r);}
    if(port_clip(rt,&dst,&clip)<0)return -1;
    int sw=sr.r-sr.l,sh=sr.b-sr.t,dw=dr.r-dr.l,dh=dr.b-dr.t;
    if(sw<=0||sh<=0||dw<=0||dh<=0||clip.r<=clip.l||clip.b<=clip.t){mac_cpu_set(rt->cpu,MAC_A7,sp+22);return 1;}
    if(sr.l<src.bounds.l||sr.t<src.bounds.t||sr.r>src.bounds.r||sr.b>src.bounds.b)
        return fail(rt,"CopyBits source rectangle exceeds source bounds.");
    size_t w=(unsigned)(clip.r-clip.l),h=(unsigned)(clip.b-clip.t);
    if(w*h>8u*1024*1024)return fail(rt,"CopyBits transfer exceeds pixel budget.");
    unsigned char*snapshot=malloc(w*h);
    if(!snapshot)return fail(rt,"CopyBits snapshot allocation failed.");
    unsigned char*ram=mac_cpu_ram(rt->cpu);
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++){
        int sx=sr.l+(int)(((int64_t)clip.l+(int)x-dr.l)*sw/dw);
        int sy=sr.t+(int)(((int64_t)clip.t+(int)y-dr.t)*sh/dh);
        unsigned v=pixel(ram,&src,sx,sy);
        if(linked_source) { /* pixel -> table entry -> palette index -> device index; refuse before writing anything */
            if(v>table_size){free(snapshot);return fail(rt,"CopyBits palette-linked pixel has no color table entry.");}
            unsigned index=rd(rt,rd(rt,src.table,4)+8+v*8,2);
            if(index>=palette_count||index>255){free(snapshot);return fail(rt,"CopyBits palette-linked entry is outside the window palette.");}
            v=rt->palette_map[index];
        }
        snapshot[y*w+x]=(unsigned char)(convert?conv[v&255]:v);
    }
    unsigned bits=dst.depth==8?255:1;
    for(size_t y=0;y<h;y++)for(size_t x=0;x<w;x++){
        unsigned s=snapshot[y*w+x],d=pixel(ram,&dst,clip.l+(int)x,clip.t+(int)y),v;
        if(mode&4)s^=bits;
        switch(mode&3){case 0:v=s;break;case 1:v=s|d;break;case 2:v=s^d;break;default:v=(~s)&d;break;}
        if(inside(clip.l+(int)x,clip.t+(int)y))put(ram,&dst,clip.l+(int)x,clip.t+(int)y,v&bits);
    }
    free(snapshot);mac_cpu_set(rt->cpu,MAC_A7,sp+22);return 1;
}

int mac_copybits_service(MacRuntime*rt,uint16_t opcode){
    int rc=copybits_core(rt,opcode);
    free_rgns();
    return rc;
}
