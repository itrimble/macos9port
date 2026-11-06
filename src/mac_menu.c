#include "mac_menu_int.h"
extern const uint8_t font8x8_basic[128][8];
extern const uint8_t font8x8_pi[8];
#include <string.h>

/* Independent rendering of our initialized, empty menu list. Public contracts:
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-114.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-130.html
 * The private two-byte list representation is ours, not an Apple structure.
 * Native title glyphs use our existing font; system-menu byte0x14 uses the
 * authored OS999 mark. Selection, custom definitions/colors are unsupported. */
static int fail(MacRuntime *r,const char *why){mac_cpu_halt(r->cpu,why);return -1;}
static int valid(MacRuntime *r,uint32_t p,size_t n,int align){
    size_t size=mac_cpu_ram_size(r->cpu);
    return p && (!align || !(p&1)) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
int mac_menu_service(MacRuntime *r,uint16_t opcode){
    if(opcode!=0xa937)return mac_menu_records_service(r,opcode);
    if(mac_cpu_halted(r->cpu))return -1;
    if(!(r->manager_flags&4) || !valid(r,0xa1c,4,1) || !valid(r,0xbaa,2,1))
        return fail(r,"DrawMenuBar requires initialized Menu Manager state.");
    unsigned count=0;MacHandle *list=mac_menu_list(r,&count);
    if(!list)return fail(r,"DrawMenuBar requires a valid owned menu list.");
    uint8_t titles[32][256];uint32_t enabled[32];unsigned total=16;
    for(unsigned i=0;i<count;i++){
        MacHandle*m=mac_menu_record(r,rd(r,list->pointer+2+4*i,4),NULL);
        uint32_t p=m->pointer;unsigned n=rd(r,p+14,1);
        memcpy(titles[i],mac_cpu_ram(r->cpu)+p+14,n+1);enabled[i]=rd(r,p+10,4)&1;
        for(unsigned j=1;j<=n;j++)if((titles[i][j]<32 && titles[i][j]!=0x14) || titles[i][j]>=127)
            return fail(r,"Menu title contains unsupported MacRoman glyphs.");
        total+=n*8+16;
    }
    if(count && total>r->screen_width)return fail(r,"Menu titles exceed the bounded menu bar width.");
    MacResource custom;
    if(mac_resource_find(&r->fork,MAC_RESOURCE_TYPE('m','c','t','b'),0,&custom)==0)
        return fail(r,"Custom menu color tables are not implemented.");
    unsigned height=rd(r,0xbaa,2),width=r->screen_width,row=r->screen_row_bytes;
    if(!width || !r->screen_height || height>r->screen_height ||
       (r->screen_depth!=1 && r->screen_depth!=8) ||
       row<(r->screen_depth==1?(width+7)/8:width) ||
       !valid(r,r->screen_base,(size_t)row*r->screen_height,0))
        return fail(r,"Menu bar screen dimensions or pixel depth are invalid.");
    /* A zero-height menu bar is explicitly hidden by the guest. */
    if(!height)return 1;
    unsigned light=0,dark=1;
    if(r->screen_depth==8){
        unsigned min=UINT32_MAX,max=0;dark=light=0;
        for(unsigned i=0;i<256;i++){
            uint32_t c=r->palette[i];unsigned v=((c>>16)&255)+((c>>8)&255)+(c&255);
            if(v<min){min=v;dark=i;}if(v>=max){max=v;light=i;}
        }
    }
    uint8_t *screen=mac_cpu_ram(r->cpu)+r->screen_base;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
        unsigned ink=y+1==height?dark:light;
        if(r->screen_depth==8)screen[(size_t)y*row+x]=(uint8_t)ink;
        else {
            uint8_t mask=(uint8_t)(0x80u>>(x&7));
            uint8_t *pixel=screen+(size_t)y*row+x/8;
            *pixel=(uint8_t)((*pixel&~mask)|(ink?mask:0));
        }
    }
    unsigned left=16,top=height>9?(height-9)/2:0;
    for(unsigned i=0;i<count;i++){
        for(unsigned k=0;k<titles[i][0];k++){
            unsigned ch=titles[i][k+1];
            const uint8_t *glyph=ch==0x14?font8x8_pi:font8x8_basic[ch];
            for(unsigned y=0;y<8 && top+y+1<height;y++)for(unsigned x=0;x<8;x++){
                unsigned px=left+k*8+x,py=top+y;
                if(!(glyph[y]&(1u<<x)) || (!enabled[i] && ((px+py)&1)))continue;
                if(r->screen_depth==8)screen[(size_t)py*row+px]=(uint8_t)dark;
                else screen[(size_t)py*row+px/8]|=(uint8_t)(0x80u>>(px&7));
            }
        }
        left+=titles[i][0]*8+16;
    }
    return 1;
}
