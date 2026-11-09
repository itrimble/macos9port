#include "mac_pict.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Public serialized PICT interface, no Apple implementation or image bytes:
 * https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-461.html
 * Version2 records are word-aligned. Indexed PackBits is scanline-local. */
enum { PIXELS=4*1024*1024, STREAM=8*1024*1024, OPS=4096 };
typedef struct { int t,l,b,r; } Box;
typedef struct { const uint8_t *p;size_t size,pos;const char *error;unsigned opcode; } Reader;
static uint16_t u16(const uint8_t*p){return (uint16_t)(p[0]*256u+p[1]);}
static uint32_t u32(const uint8_t*p){return (uint32_t)u16(p)<<16|u16(p+2);}
static Box box(const uint8_t*p){return (Box){(int16_t)u16(p),(int16_t)u16(p+2),(int16_t)u16(p+4),(int16_t)u16(p+6)};}
static const uint8_t *take(Reader*r,size_t n){if(n>r->size-r->pos){r->error="Truncated PICT stream.";return NULL;}const uint8_t*p=r->p+r->pos;r->pos+=n;return p;}
static int reject(Reader*r,const char*s){r->error=s;return 0;}
static int positive(Box b){return b.r>b.l&&b.b>b.t;}
static int contains(Box a,Box b){return b.l>=a.l&&b.t>=a.t&&b.r<=a.r&&b.b<=a.b;}
static Box intersection(Box a,Box b){if(a.l<b.l)a.l=b.l;if(a.t<b.t)a.t=b.t;if(a.r>b.r)a.r=b.r;if(a.b>b.b)a.b=b.b;return a;}
static unsigned match(const MacPictPalette*p,const uint8_t *rgb){
    uint64_t best=UINT64_MAX;unsigned index=256;
    for(unsigned i=0;i<256;i++)if(p->allowed[i]){
        uint64_t distance=0;for(unsigned c=0;c<3;c++){int64_t delta=(int64_t)u16(rgb+c*2)-p->rgb[i][c];distance+=(uint64_t)(delta*delta);}
        if(distance<best){best=distance;index=i;}
    }
    if(p->exact && best)return 256; /* 1-bit ports: no approximation of other colours */
    return index;
}
static int unpack(Reader*r,unsigned row,unsigned height,uint8_t *out){
    for(unsigned y=0;y<height;y++){
        if(row<8){const uint8_t*p=take(r,row);if(!p)return 0;memcpy(out+(size_t)y*row,p,row);continue;}
        const uint8_t*p=take(r,row>250?2:1);if(!p)return 0;
        unsigned bytes=row>250?u16(p):*p;
        p=take(r,bytes);if(!p)return 0;unsigned at=0,written=0;
        while(at<bytes){
            int control=(int8_t)p[at++];if(control==-128)continue;
            unsigned n=control>=0?(unsigned)control+1:(unsigned)(1-control);
            if(n>row-written)return reject(r,"PackBits run exceeds its scanline.");
            if(control>=0){if(n>bytes-at)return reject(r,"PackBits literal exceeds its record.");memcpy(out+(size_t)y*row+written,p+at,n);at+=n;}
            else {if(at==bytes)return reject(r,"PackBits repeat has no byte.");memset(out+(size_t)y*row+written,p[at++],n);}
            written+=n;
        }
        if(written!=row)return reject(r,"PackBits scanline has the wrong decoded length.");
    }return 1;
}
/* The palette index of pixel (x, y), counted from the PixMap's bounds origin, in a row of `bits`-bit pixels packed
 * most-significant-bits first. Bits past the last pixel of a row are padding, not color data. */
static unsigned index_at(const uint8_t*decoded,unsigned row,unsigned bits,unsigned x,unsigned y){
    unsigned offset=x*bits;
    return (decoded[(size_t)y*row+offset/8]>>(8-bits-offset%8))&((1u<<bits)-1u);
}
/* Ceiling division handles rectangles extending left/above the picture. */
static int ceil_ratio(int64_t n,int d){return (int)(n>=0?(n+d-1)/d:n/d);}
/* Scale the part of `src` (in `bounds` coordinates, drawn into `dst`) that lies inside frame, clip and dst into the
 * output image, mapping each source index through `mapping`. */
static void paint(MacPictImage*out,Box frame,Box clip,Box bounds,Box src,Box dst,const uint8_t*decoded,
                  unsigned row,unsigned bits,const uint8_t*mapping){
    Box area=intersection(intersection(frame,clip),dst);
    if(positive(area)){
        int fw=frame.r-frame.l,fh=frame.b-frame.t;
        int l=ceil_ratio((int64_t)(area.l-frame.l)*out->width,fw),t=ceil_ratio((int64_t)(area.t-frame.t)*out->height,fh);
        int right=ceil_ratio((int64_t)(area.r-frame.l)*out->width,fw),bottom=ceil_ratio((int64_t)(area.b-frame.t)*out->height,fh);
        for(int y=t;y<bottom;y++)for(int x=l;x<right;x++){
            int64_t xx=(int64_t)x*fw+(int64_t)(frame.l-dst.l)*out->width;
            int64_t yy=(int64_t)y*fh+(int64_t)(frame.t-dst.t)*out->height;
            int sx=src.l+(int)(xx*(src.r-src.l)/((int64_t)out->width*(dst.r-dst.l)));
            int sy=src.t+(int)(yy*(src.b-src.t)/((int64_t)out->height*(dst.b-dst.t)));
            size_t at=(size_t)y*out->width+(unsigned)x;
            out->pixels[at]=mapping[index_at(decoded,row,bits,(unsigned)(sx-bounds.l),(unsigned)(sy-bounds.t))];out->covered[at]=1;
        }
    }
}
static int bitmap(Reader*r,Box frame,Box clip,MacPictImage*out,const MacPictPalette*palette,size_t *budget){
    const uint8_t*p=take(r,46);if(!p)return 0;
    unsigned row=u16(p)&0x3fff;Box bounds=box(p+2);
    unsigned bits=u16(p+28); /* pixelSize: indexed PixMaps of 1, 2, 4 or 8 bits per pixel */
    if((u16(p)&0xc000)!=0x8000 || !row || (row&1) || !positive(bounds) ||
       u16(p+10)!=0 || u16(p+12)!=0 || u32(p+14)!=0 ||
       u16(p+26)!=0 || !(bits==1 || bits==2 || bits==4 || bits==8) || u16(p+30)!=1 || u16(p+32)!=bits || u32(p+34)!=0 || u32(p+42)!=0 ||
       (uint64_t)(bounds.r-bounds.l)*bits>(uint64_t)row*8)
        return reject(r,"PICT requires unpacked-layout indexed PixMaps of 1, 2, 4 or 8 bits with default packing.");
    size_t pixels=(size_t)row*(unsigned)(bounds.b-bounds.t);
    if(pixels>PIXELS || pixels>PIXELS*2u-*budget)return reject(r,"PICT decoded pixel budget exceeded.");
    *budget+=pixels;
    p=take(r,8);if(!p)return 0;unsigned flags=u16(p+4),count=(unsigned)u16(p+6)+1;
    if((flags&~0x8000u)||count>256)return reject(r,"PICT color table flags or length unsupported.");
    uint8_t mapping[256]={0},defined[256]={0};
    for(unsigned i=0;i<count;i++){
        p=take(r,8);if(!p)return 0;unsigned index=flags&0x8000?i:u16(p);
        if(index>255 || defined[index])return reject(r,"PICT source color indices are invalid or duplicated.");
        unsigned mapped=match(palette,p+2);if(mapped==256)return reject(r,"No destination palette color is available (1-bit ports need an exact black or white).");
        defined[index]=1;mapping[index]=(uint8_t)mapped;
    }
    p=take(r,18);if(!p)return 0;Box src=box(p),dst=box(p+8);
    if(u16(p+16)!=0 || !positive(src) || !positive(dst) || !contains(bounds,src))
        return reject(r,"PICT requires bounded positive rectangles and srcCopy.");
    uint8_t *decoded=malloc(pixels);if(!decoded)return reject(r,"PICT scanline allocation failed.");
    if(!unpack(r,row,(unsigned)(bounds.b-bounds.t),decoded)){free(decoded);return 0;}
    /* Validate every actual pixel, including clipped pixels; row padding is not color data. */
    for(int y=0;y<bounds.b-bounds.t;y++)for(int x=0;x<bounds.r-bounds.l;x++)
        if(!defined[index_at(decoded,row,bits,(unsigned)x,(unsigned)y)]){free(decoded);return reject(r,"PICT pixels reference undefined source colors.");}
    paint(out,frame,clip,bounds,src,dst,decoded,row,bits,mapping);
    free(decoded);return 1;
}
/* Version-1 BitsRect (0x90) / PackBitsRect (0x98): a plain 1-bit BitMap, 1 = black, 0 = white. */
static int bitmap_v1(Reader*r,int packed,Box frame,Box clip,MacPictImage*out,const MacPictPalette*palette,size_t *budget){
    const uint8_t*p=take(r,28);if(!p)return 0;
    unsigned row=u16(p);Box bounds=box(p+2),src=box(p+10),dst=box(p+18);
    if((row&0xc000) || !row || (row&1) || !positive(bounds) || u16(p+26)!=0 ||
       (uint64_t)(bounds.r-bounds.l+7)/8>row)
        return reject(r,"PICT version 1 requires an even-rowBytes BitMap with srcCopy.");
    if(!positive(src) || !positive(dst) || !contains(bounds,src))return reject(r,"PICT requires bounded positive rectangles and srcCopy.");
    size_t pixels=(size_t)row*(unsigned)(bounds.b-bounds.t);
    if(pixels>PIXELS || pixels>PIXELS*2u-*budget)return reject(r,"PICT decoded pixel budget exceeded.");
    *budget+=pixels;
    static const uint8_t white[6]={255,255,255,255,255,255},black[6]={0};
    unsigned w=match(palette,white),k=match(palette,black);
    if(w==256 || k==256)return reject(r,"No destination palette color is available (1-bit ports need an exact black or white).");
    uint8_t mapping[2]={(uint8_t)w,(uint8_t)k};
    uint8_t *decoded=malloc(pixels);if(!decoded)return reject(r,"PICT scanline allocation failed.");
    if(packed){if(!unpack(r,row,(unsigned)(bounds.b-bounds.t),decoded)){free(decoded);return 0;}}
    else {const uint8_t*d=take(r,pixels);if(!d){free(decoded);return 0;}memcpy(decoded,d,pixels);}
    paint(out,frame,clip,bounds,src,dst,decoded,row,1,mapping);
    free(decoded);return 1;
}
void mac_pict_destroy(MacPictImage*i){if(i){free(i->pixels);free(i->covered);memset(i,0,sizeof *i);}}
int mac_pict_decode(const uint8_t *data,size_t size,unsigned width,unsigned height,
                    const MacPictPalette *palette,MacPictImage *out,char *error,size_t n){
    if(!out)return -1;memset(out,0,sizeof *out);
    Reader r={data,size,0,NULL,0};const uint8_t*p;
    if(!data || !palette || size>STREAM || !width || !height || (uint64_t)width*height>PIXELS){r.error="PICT input or output exceeds bounded limits.";goto bad;}
    p=take(&r,12);if(!p)goto bad;Box frame=box(p+2),clip=frame;
    int v1=u16(p+10)==0x1101; /* version 1: single-byte opcodes, no alignment */
    if(!v1 && !take(&r,2))goto bad; /* version 2: 0011 02ff */
    if(!positive(frame)||(!v1 && (u16(p+10)!=0x11||u16(p+12)!=0x02ff))){r.error="Only positive-frame PICT versions 1 and 2 are supported.";goto bad;}
    out->width=width;out->height=height;out->pixels=malloc((size_t)width*height);out->covered=calloc((size_t)width*height,1);
    if(!out->pixels||!out->covered){r.error="PICT output allocation failed.";goto bad;}
    size_t budget=0;unsigned operations=0;int header=0;
    if(v1){
        while(r.pos<r.size && operations++<OPS){
            p=take(&r,1);if(!p)goto bad;unsigned op=*p;r.opcode=op;
            if(op==0xff){if(r.pos!=r.size){r.error="PICT end marker or trailing bytes are invalid.";goto bad;}if(error&&n)*error=0;return 0;}
            else if(op==0)continue;
            else if(op==0xa0){if(!take(&r,2))goto bad;}
            else if(op==1){p=take(&r,10);if(!p)goto bad;if(u16(p)!=10){r.error="PICT complex clipping regions are unsupported.";goto bad;}clip=box(p+2);}
            else if(op==0x90 || op==0x98){if(!bitmap_v1(&r,op==0x98,frame,clip,out,palette,&budget))goto bad;}
            else {r.error="Unsupported PICT version 1 opcode.";goto bad;}
        }
        r.error="PICT has no end marker or exceeds the opcode budget.";goto bad;
    }
    while(r.pos<r.size && operations++<OPS){
        if(r.pos&1){p=take(&r,1);if(!p)goto bad;if(*p){r.error="PICT alignment padding is nonzero.";goto bad;}}
        p=take(&r,2);if(!p)goto bad;unsigned op=u16(p);r.opcode=op;
        if(op==0xff){if(!header || r.pos!=r.size){r.error="PICT end marker or trailing bytes are invalid.";goto bad;}if(error&&n)*error=0;return 0;}
        if(op==0x0c00){
            p=take(&r,24);if(!p)goto bad;
            if(header){r.error="PICT header requires matching version2 fixed bounds.";goto bad;}
            if(u32(p)==UINT32_MAX){   /* version 2: Version -1, then the picture frame as four Fixed values, then a reserved long */
                if((int32_t)u32(p+4)!=(int64_t)frame.l*65536 || (int32_t)u32(p+8)!=(int64_t)frame.t*65536 ||
                   (int32_t)u32(p+12)!=(int64_t)frame.r*65536 || (int32_t)u32(p+16)!=(int64_t)frame.b*65536 || u32(p+20)){
                    r.error="PICT header requires matching version2 fixed bounds.";goto bad;}
            } else {                  /* extended version 2 (Apple's Imaging With QuickDraw, Appendix A, Listing A-5): Version -2, a reserved word, the best
                                       * horizontal and vertical resolution as Fixed, the optimal source rectangle, a reserved long. Only 72 dpi pictures are
                                       * drawn; the source rectangle is not used (the picture frame is) but must not be empty. */
                if(u16(p)!=0xfffe || u16(p+2) || u32(p+20)){r.error="PICT extended version 2 header is malformed.";goto bad;}
                if(u32(p+4)!=0x00480000u || u32(p+8)!=0x00480000u){r.error="PICT extended version 2 resolutions other than 72 dpi are unsupported.";goto bad;}
                Box s=box(p+12);
                if(s.t>=s.b || s.l>=s.r){r.error="PICT extended version 2 source rectangle is empty.";goto bad;}
            }
            header=1;
        }else if(!header){r.error="PICT opcode precedes its HeaderOp.";goto bad;}
        else if(op==0 || op==0x1e)continue;
        else if(op==0xa0){if(!take(&r,2))goto bad;} /* Default comment procedure ignores comments. */
        else if(op==1){p=take(&r,10);if(!p)goto bad;if(u16(p)!=10){r.error="PICT complex clipping regions are unsupported.";goto bad;}clip=box(p+2);}
        else if(op==0x98){if(!bitmap(&r,frame,clip,out,palette,&budget))goto bad;}
        else {r.error="Unsupported PICT opcode.";goto bad;}
    }
    r.error="PICT has no end marker or exceeds the opcode budget.";
bad:
    if(error&&n)snprintf(error,n,"%s (opcode %04x, byte %zu)",r.error?r.error:"Invalid PICT.",r.opcode,r.pos);mac_pict_destroy(out);return -1;
}
