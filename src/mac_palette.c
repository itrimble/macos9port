#include "mac_runtime.h"
#include <string.h>

/* Independent indexed Color Manager subset from public ABI documentation:
 * leopard-adc.pepas.com/documentation/mac/ACIReference/ACIReference-359.html
 * No hardware drivers or inverse-table callbacks. Reservation ownership uses
 * the documented ColorSpec.value low byte; the high byte is preserved.
 * https://leopard-adc.pepas.com/documentation/mac/ACIReference/ACIReference-358.html */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] << 8 | p[1]); }
static int fail(MacRuntime *rt, const char *s) { mac_cpu_halt(rt->cpu,s); return -1; }
static int valid(MacRuntime *rt, uint32_t p, size_t n) {
    size_t size=mac_cpu_ram_size(rt->cpu);
    return p && !(p&1) && p<=size && n<=size-p;
}
static uint32_t rd(MacRuntime *rt,uint32_t p,unsigned n) { return mac_cpu_read(rt->cpu,p,n); }
static void wr(MacRuntime *rt,uint32_t p,unsigned n,uint32_t v) { mac_cpu_write(rt->cpu,p,n,v); }
static int owned(MacRuntime *rt,uint32_t h,uint32_t n,uint32_t *p) {
    if(!valid(rt,h,4))return 0;
    *p=rd(rt,h,4);
    if(!valid(rt,*p,n))return 0;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *m=&rt->handles[i];
        if(m->handle==h && m->pointer==*p && m->size>=n)return 1;
    }
    return 0;
}
static int device_table(MacRuntime *rt,uint32_t *gd,uint32_t *ct) {
    uint32_t pm;
    if(!rt->main_device || rt->current_device!=rt->main_device || rt->screen_depth!=8 ||
       !owned(rt,rt->current_device,62,gd) || rd(rt,*gd+4,2)!=0 ||
       rd(rt,*gd+22,4)!=rt->screen_pixmap || !owned(rt,rt->screen_pixmap,50,&pm) ||
       rd(rt,pm+32,2)!=8 || rd(rt,pm+42,4)!=rt->screen_colors ||
       !owned(rt,rt->screen_colors,2056,ct) || rd(rt,*ct+6,2)!=255 ||
       !(rd(rt,*ct+4,2)&0x8000))
        return fail(rt,"Palette operation requires the owned 8-bit screen color table.");
    return 1;
}
int mac_palette_match_mask(MacRuntime *rt,uint8_t allowed[256]) {
    uint32_t gd,ct;
    if(device_table(rt,&gd,&ct)<0)return -1;
    unsigned client=rd(rt,gd+2,2)&255;
    for(unsigned i=0;i<256;i++) {
        unsigned owner=rd(rt,ct+8+i*8,2)&255;
        allowed[i]=(uint8_t)(!owner || owner==client);
    }
    return 1;
}
static int reserve_entry(MacRuntime *rt,uint32_t sp) {
    uint32_t gd,ct;
    int index=(int16_t)rd(rt,sp+2,2);
    unsigned reserve=rd(rt,sp,1); /* Pascal Boolean occupies a padded word. */
    if(index<0 || index>255) { rt->qd_error=-153;return 1; }
    if(device_table(rt,&gd,&ct)<0)return -1;
    uint32_t entry=ct+8+(unsigned)index*8;
    unsigned value=rd(rt,entry,2),owner=value&255,client=rd(rt,gd+2,2)&255;
    if(reserve && owner && owner!=client) { rt->qd_error=-154;return 1; }
    rt->qd_error=0;
    unsigned next=(value&0xff00)|(reserve?client:0);
    wr(rt,entry,2,next);
    uint32_t seed=rd(rt,ct,4)+1;
    wr(rt,ct,4,seed<1024?1024:seed);
    return 1;
}
static int set_entries(MacRuntime *rt,uint32_t sp) {
    uint32_t input=rd(rt,sp,4);
    int count=(int16_t)rd(rt,sp+4,2), start=(int16_t)rd(rt,sp+6,2);
    if(count<0 || count>255 || start< -1 || start>255 ||
       (start>=0 && start+count>255) || !valid(rt,input,(size_t)(count+1)*8))
        return fail(rt,"SetEntries has an invalid index, count, or ColorSpec array.");
    uint32_t gd,ct;
    if(device_table(rt,&gd,&ct)<0)return -1;
    /* Snapshot the input so guest input overlapping the destination is safe. */
    uint8_t entries[2048]; unsigned indices[256];
    memcpy(entries,mac_cpu_ram(rt->cpu)+input,(size_t)(count+1)*8);
    for(int i=0;i<=count;i++) {
        indices[i]=start<0?be16(entries+i*8):(unsigned)(start+i);
        if(indices[i]>255)return fail(rt,"SetEntries ColorSpec index exceeds the device palette.");
        unsigned value=rd(rt,ct+8+indices[i]*8,2);
        if((value&0xff00) || ((value&255) && (value&255)!=(rd(rt,gd+2,2)&255)))
            return fail(rt,"SetEntries target is protected or reserved by another client.");
    }
    uint16_t client=(uint16_t)rd(rt,gd+2,2);
    for(int i=0;i<=count;i++) {
        const uint8_t *rgb=entries+i*8+2;
        uint32_t dst=ct+8+indices[i]*8;
        wr(rt,dst,2,client);
        memcpy(mac_cpu_ram(rt->cpu)+dst+2,rgb,6);
        rt->palette[indices[i]]=(uint32_t)rgb[0]<<16|(uint32_t)rgb[2]<<8|rgb[4];
    }
    uint32_t seed=rd(rt,ct,4)+1;
    wr(rt,ct,4,seed<1024?1024:seed);
    return 1;
}
static uint32_t table_handle(MacRuntime *rt,const uint8_t *data,uint32_t size) {
    size_t slot=0;
    while(slot<MAC_RUNTIME_HANDLES && (rt->handles[slot].handle || rt->handles[slot].pointer))slot++;
    if(slot==MAC_RUNTIME_HANDLES)return 0;
    uint32_t saved=rt->heap_next,h=mac_runtime_alloc(rt,4),p=mac_runtime_alloc(rt,size);
    if(!h || !p) { rt->heap_next=saved; return 0; }
    memcpy(mac_cpu_ram(rt->cpu)+p,data,size);wr(rt,h,4,p);
    rt->handles[slot]=(MacHandle){.handle=h,.pointer=p,.size=size};
    if(rt->handle_count<=slot)rt->handle_count=slot+1;
    return h;
}
static int get_table(MacRuntime *rt,uint32_t sp) {
    int16_t id=(int16_t)rd(rt,sp,2); MacResource r;
    uint8_t authored[2056]={0}; const uint8_t *data=NULL; uint32_t size=0;
    int found=mac_resource_find(&rt->fork,MAC_RESOURCE_TYPE('c','l','u','t'),id,&r)==0;
    if(found) {
        if(r.size<8 || be16(r.data+6)>255 || r.size<8u+(be16(r.data+6)+1u)*8u)
            return fail(rt,"GetCTable encountered a malformed indexed clut resource.");
        size=8u+(be16(r.data+6)+1u)*8u; data=r.data;
    } else if(id==8) {
        /* Authored RGB332 cube; deliberately not Apple's historical palette. */
        authored[3]=8; authored[7]=255;
        for(unsigned i=0;i<256;i++) {
            unsigned v=255-i, rgb[3]={((v>>5)&7)*255/7,((v>>2)&7)*255/7,(v&3)*255/3};
            authored[8+i*8+1]=(uint8_t)i;
            for(unsigned c=0;c<3;c++)authored[10+i*8+c*2]=authored[11+i*8+c*2]=(uint8_t)rgb[c];
        }
        data=authored;size=sizeof authored;
    }
    uint32_t h=data?table_handle(rt,data,size):0;
    wr(rt,sp+2,4,h);wr(rt,0xa60,2,(uint16_t)(h?0:data?-108:-192));
    if(data)wr(rt,0x220,2,(uint16_t)(h?0:-108));
    return 1;
}
int mac_palette_service(MacRuntime *rt,uint16_t opcode) {
    if(opcode!=0xaa3f && opcode!=0xaa18 && opcode!=0xaa3e && opcode!=0xaa40)return 0;
    if(mac_cpu_halted(rt->cpu))return -1;
    uint32_t sp=mac_cpu_get(rt->cpu,MAC_A7);
    if(opcode==0xaa40) {
        if(!valid(rt,sp,2))return fail(rt,"QDError result exceeds guest stack.");
        wr(rt,sp,2,(uint16_t)rt->qd_error);return 1;
    }
    if(!valid(rt,sp,opcode==0xaa3f?8:opcode==0xaa3e?4:6))return fail(rt,"Palette call stack is outside aligned RAM.");
    int result=opcode==0xaa3f?set_entries(rt,sp):opcode==0xaa3e?reserve_entry(rt,sp):get_table(rt,sp);
    if(result>0)mac_cpu_set(rt->cpu,MAC_A7,sp+(opcode==0xaa3f?8:opcode==0xaa3e?4:2));
    return result;
}
