#include "mac_runtime.h"
#include "mac_pict.h"
#include <string.h>
#include <stdio.h>
/* Pascal DrawPicture(picHandle,dstRect), A8F6. CPU indexed playback only.
 * https://leopard-adc.pepas.com/documentation/mac/QuickDraw/QuickDraw-350.html */
typedef struct {int t,l,b,r;} Box;
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static int valid(MacRuntime*r,uint32_t p,size_t n,int aligned){size_t z=mac_cpu_ram_size(r->cpu);return p && (!aligned||!(p&1)) && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static Box box(MacRuntime*r,uint32_t p){return (Box){(int16_t)rd(r,p,2),(int16_t)rd(r,p+2,2),(int16_t)rd(r,p+4,2),(int16_t)rd(r,p+6,2)};}
static Box intersect(Box a,Box b){if(a.t<b.t)a.t=b.t;if(a.l<b.l)a.l=b.l;if(a.b>b.b)a.b=b.b;if(a.r>b.r)a.r=b.r;return a;}
static int handle(MacRuntime*r,uint32_t h,unsigned size,uint32_t*p){
    if(!valid(r,h,4,1))return 0;*p=rd(r,h,4);return valid(r,*p,size,1);
}
static int region(MacRuntime*r,uint32_t h,Box*b){uint32_t p;if(!handle(r,h,10,&p)||rd(r,p,2)!=10)return 0;*b=box(r,p+2);return 1;}
static int palette(MacRuntime*r,uint32_t pm,uint32_t base,MacPictPalette*out){
    memset(out,0,sizeof *out);
    if(base==r->screen_base){
        if(r->screen_depth!=8 || mac_palette_match_mask(r,out->allowed)<0)return 0;
        for(unsigned i=0;i<256;i++)for(unsigned c=0;c<3;c++)out->rgb[i][c]=(uint16_t)(((r->palette[i]>>(16-c*8))&255)*257);
        return 1;
    }
    uint32_t p;if(!handle(r,rd(r,pm+42,4),2056,&p) || rd(r,p+6,2)!=255 || (rd(r,p+4,2)&0x7fff))return 0;
    unsigned flags=rd(r,p+4,2);
    for(unsigned i=0;i<256;i++){
        unsigned index=flags&0x8000?i:rd(r,p+8+i*8,2);
        if(index>255 || out->allowed[index])return 0;
        out->allowed[index]=1;for(unsigned c=0;c<3;c++)out->rgb[index][c]=(uint16_t)rd(r,p+10+i*8+c*2,2);
    }return 1;
}
int mac_picture_service(MacRuntime*r,uint16_t opcode){
    if(opcode!=0xa8f6)return 0;
    if(!r || !r->cpu || mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if(!valid(r,sp,8,1))return fail(r,"DrawPicture stack is invalid.");
    uint32_t dst=rd(r,sp,4),pic=rd(r,sp+4,4);
    if(!valid(r,dst,8,1)||!valid(r,pic,4,1))return fail(r,"DrawPicture rectangle or picture handle is invalid.");
    MacHandle*record=NULL;
    for(size_t i=0;i<r->handle_count;i++)if(r->handles[i].handle==pic){record=&r->handles[i];break;}
    if(!record || record->loaded_code || (record->type && record->type!=MAC_RESOURCE_TYPE('P','I','C','T')) ||
       rd(r,pic,4)!=record->pointer || record->size<14 || !valid(r,record->pointer,record->size,1))
        return fail(r,"DrawPicture requires a live owned picture handle.");
    uint32_t port=r->current_port;
    if(r->qd_globals){if(!valid(r,r->qd_globals,4,1))return fail(r,"DrawPicture QuickDraw globals are invalid.");port=rd(r,r->qd_globals,4);}
    if(!valid(r,port,108,1)||(rd(r,port+6,2)&0xc000)!=0xc000 || rd(r,port+104,4))
        return fail(r,"DrawPicture requires a color port with native drawing procedures.");
    if(rd(r,port+36,4)||rd(r,port+40,2)||rd(r,port+42,4)!=UINT32_MAX||rd(r,port+46,2)!=65535)
        return fail(r,"DrawPicture nondefault foreground/background mapping is unsupported.");
    uint32_t pm;if(!handle(r,rd(r,port+2,4),50,&pm))return fail(r,"DrawPicture destination PixMap is invalid.");
    unsigned row=rd(r,pm+4,2);
    if((row&0xc000)!=0x8000 || (row&1) || rd(r,pm+16,2) || rd(r,pm+30,2) ||
       rd(r,pm+32,2)!=8 || rd(r,pm+34,2)!=1 || rd(r,pm+36,2)!=8)
        return fail(r,"DrawPicture destination must be unpacked indexed8.");
    row&=0x3fff;uint32_t base=rd(r,pm,4);Box bounds=box(r,pm+6),dr=box(r,dst),clip=intersect(bounds,box(r,port+16)),rg;
    int w=bounds.r-bounds.l,h=bounds.b-bounds.t;
    if(w<0 || h<0 || (unsigned)w>row || (uint64_t)row*(unsigned)h>8u*1024*1024 || !valid(r,base,(size_t)row*(unsigned)h,0))
        return fail(r,"DrawPicture destination extent is invalid.");
    for(unsigned off=24;off<=28;off+=4){if(!region(r,rd(r,port+off,4),&rg))return fail(r,"DrawPicture requires rectangular clipping regions.");clip=intersect(clip,rg);}
    clip=intersect(clip,dr);
    MacPictPalette colors;if(!palette(r,pm,base,&colors))return fail(r,"DrawPicture destination palette is invalid or unsupported.");
    int dw=dr.r-dr.l,dh=dr.b-dr.t;MacPictImage image;char error[160];
    if(mac_pict_decode(mac_cpu_ram(r->cpu)+record->pointer,record->size,dw>0?(unsigned)dw:1,dh>0?(unsigned)dh:1,&colors,&image,error,sizeof error))
        return fail(r,error);
    /* No guest pixels or port state have changed before complete parse success. */
    size_t painted=0;
    if(dw>0 && dh>0)for(int y=clip.t;y<clip.b;y++)for(int x=clip.l;x<clip.r;x++){
        size_t i=(size_t)(y-dr.t)*(unsigned)dw+(unsigned)(x-dr.l);
        if(image.covered[i]){mac_cpu_ram(r->cpu)[base+(size_t)(y-bounds.t)*row+(unsigned)(x-bounds.l)]=image.pixels[i];painted++;}
    }
    if(r->trace)fprintf(stderr,"DrawPicture bytes=%u output=%dx%d clip=%d,%d,%d,%d painted=%zu\n",record->size,dw,dh,clip.t,clip.l,clip.b,clip.r,painted);
    mac_pict_destroy(&image);mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
}
