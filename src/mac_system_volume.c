#include "mac_system_volume.h"
#include "mac_drive_queue.h"
#include "mac_files_int.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Authored in-memory system catalog, not an Apple System file or ROM.
 * Public PB layouts/semantics: Inside Macintosh: Files, pp.2-108,2-203,
 * 2-237,2-292..296; resource fork container: Resource Manager reference.
 * https://leopard-adc.pepas.com/documentation/mac/pdf/Files/File_Manager.pdf
 * One readonly file, one real 512-byte allocation; no raw disk driver.
 */
enum { VREF = -2, FIRST_REF = 64, OPEN_COUNT = 8, FORK_BYTES = 286 };
static const char vol_name[] = "OS Pi System";
typedef struct { int open, resource; uint32_t mark; } OpenFile;
struct MacSystemVolume {
    uint8_t allocation[512];
    uint32_t created;
    OpenFile files[OPEN_COUNT];
};
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0]*256u+p[1]); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p)<<16|r16(p+2); }
static void w16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static void w32(uint8_t *p,uint32_t v) { w16(p,(uint16_t)(v>>16));w16(p+2,(uint16_t)v); }
static int range(MacCpu *c,uint32_t p,size_t n) {
    return p<=mac_cpu_ram_size(c) && n<=mac_cpu_ram_size(c)-p;
}
static int done(MacCpu *c,uint8_t *pb,int error) {
    w16(pb+16,(uint16_t)error);
    mac_cpu_set(c,MAC_D0,(uint32_t)(int32_t)error);
    mac_cpu_set(c,MAC_SR,(mac_cpu_get(c,MAC_SR)&~15u)|(error<0?8u:error==0?4u:0u));
    return 1;
}
static int equal_name(const uint8_t *p,const char *name) {
    size_t n=strlen(name);
    if(p[0]!=n)return 0;
    for(size_t i=0;i<n;i++) {
        unsigned a=p[i+1],b=(unsigned char)name[i];
        if(a>='A'&&a<='Z')a+='a'-'A';
        if(b>='A'&&b<='Z')b+='a'-'A';
        if(a!=b)return 0;
    }
    return 1;
}
static int output_name(MacCpu *c,uint32_t pb,size_t pb_size,uint32_t p,const char *name) {
    size_t n=strlen(name)+1;
    if(!p)return 1;
    return range(c,p,n) && !((uint64_t)p<pb+pb_size && (uint64_t)p+n>pb);
}
static void put_name(MacCpu *c,uint32_t p,const char *name) {
    if(p){size_t n=strlen(name);mac_cpu_ram(c)[p]=(uint8_t)n;memcpy(mac_cpu_ram(c)+p+1,name,n);}
}
MacSystemVolume *mac_system_volume_create(MacRuntime *rt) {
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu) || !range(rt->cpu,0xad8,7))return NULL;
    int64_t now=(int64_t)time(NULL)+INT64_C(2082844800);
    if(now<0 || now>UINT32_MAX)return NULL;
    MacSystemVolume *v=calloc(1,sizeof *v);
    if(!v)return NULL;
    uint8_t *b=v->allocation;
    w32(b,256);w32(b+4,256);w32(b+8,0);w32(b+12,30);
    memcpy(b+256,b,16);w16(b+280,28);w16(b+282,30);w16(b+284,0xffff);
    MacResourceFork fork;
    if(mac_resource_open(&fork,b,FORK_BYTES,NULL,0) || mac_resource_count(&fork)) {free(v);return NULL;}
    v->created=(uint32_t)now;v->files[0]=(OpenFile){1,1,0};
    mac_cpu_write(rt->cpu,0xa58,2,FIRST_REF);
    put_name(rt->cpu,0xad8,"System");
    return v;
}
void mac_system_volume_destroy(MacSystemVolume *v) { free(v); }

static int read_file(MacCpu *c,MacSystemVolume *v,OpenFile *f,uint8_t *pb) {
    int32_t requested=(int32_t)r32(pb+36),offset=(int32_t)r32(pb+46);
    unsigned mode=r16(pb+44);uint32_t buffer=r32(pb+32),length=f->resource?FORK_BYTES:0;
    if(requested<0 || !range(c,buffer,(uint32_t)requested) || (mode&~0x33u))return -50;
    uint32_t address=(uint32_t)(pb-mac_cpu_ram(c));
    if(requested && (uint64_t)buffer<address+50u && (uint64_t)buffer+(uint32_t)requested>address)return -50;
    int64_t mark=f->mark;
    switch(mode&3){case 1:mark=offset;break;case 2:mark=(int64_t)length+offset;break;case 3:mark+=offset;break;}
    if(mark<0)return -40;
    if(mark>INT32_MAX)return -50;
    if(mark>length)mark=length;
    uint32_t count=length-(uint32_t)mark;
    if(count>(uint32_t)requested)count=(uint32_t)requested;
    if(count)memcpy(mac_cpu_ram(c)+buffer,v->allocation+(size_t)mark,count);
    f->mark=(uint32_t)mark+count;w32(pb+40,count);w32(pb+46,f->mark);
    return count<(uint32_t)requested?-39:0;
}
static int file_control(MacCpu *c,MacSystemVolume *v,uint8_t *pb,unsigned call) {
    int index=(int16_t)r16(pb+24)-FIRST_REF;
    if(index<0 || index>=OPEN_COUNT || !v->files[index].open)return -51;
    OpenFile *f=&v->files[index];uint32_t length=f->resource?FORK_BYTES:0;
    if(call==1){if(!index)return -54;f->open=0;return 0;}
    if(call==2)return read_file(c,v,f,pb);
    if(call==0x11){w32(pb+28,length);return 0;}
    if(call==0x18){w32(pb+46,f->mark);return 0;}
    if(call==0x44){
        int64_t mark=f->mark;int32_t off=(int32_t)r32(pb+46);unsigned mode=r16(pb+44);
        if(mode>3)return -50;
        if(mode==1)mark=off;else if(mode==2)mark=(int64_t)length+off;else if(mode==3)mark+=off;
        if(mark<0)return -40;
        if(mark>length)return -39;
        f->mark=(uint32_t)mark;w32(pb+46,f->mark);return 0;
    }
    return -46; /* All mutation calls routed here are volume locked. */
}
static int open_file(MacCpu *c,MacSystemVolume *v,uint8_t *pb,unsigned opcode) {
    uint32_t name=r32(pb+18);
    if(!name || !range(c,name,1) || !range(c,name,1u+mac_cpu_ram(c)[name]))return -50;
    if(!equal_name(mac_cpu_ram(c)+name,"System"))return -43;
    if((opcode&0x200) && r32(pb+48)!=0 && r32(pb+48)!=2)return -120;
    if(pb[27]>1)return -54;
    unsigned i;
    for(i=1;i<OPEN_COUNT;i++)if(!v->files[i].open)break;
    if(i==OPEN_COUNT)return -42;
    v->files[i]=(OpenFile){1,(opcode&0xff)==0x0a,0};w16(pb+24,FIRST_REF+i);return 0;
}
static int fcb_info(MacCpu *c,MacSystemVolume *v,uint32_t address,uint8_t *pb) {
    int ref=(int16_t)r16(pb+24),index=(int16_t)r16(pb+28);
    if(index<0)return -51;
    if(index>0){
        if((int16_t)r16(pb+22)!=VREF)return -35;
        int n=0;ref=0;
        for(unsigned i=0;i<OPEN_COUNT;i++)if(v->files[i].open && ++n==index){ref=FIRST_REF+(int)i;break;}
    }
    int slot=ref-FIRST_REF;
    if(slot<0 || slot>=OPEN_COUNT || !v->files[slot].open)return -51;
    uint32_t name=r32(pb+18);
    if(!output_name(c,address,62,name,"System"))return -50;
    OpenFile *f=&v->files[slot];
    w16(pb+24,(uint16_t)ref);w32(pb+32,16);w16(pb+36,0x6000|(f->resource?0x200:0));
    w16(pb+38,0);w32(pb+40,f->resource?FORK_BYTES:0);w32(pb+44,f->resource?512:0);
    w32(pb+48,f->mark);w16(pb+52,(uint16_t)VREF);w32(pb+54,512);w32(pb+58,2);
    put_name(c,name,"System");return 0;
}
static int wd_info(MacCpu *c,uint32_t address,uint8_t *pb,unsigned selector) {
    uint32_t name=r32(pb+18);
    if(selector==2)return 0; /* Closing a volume reference does nothing. */
    if(selector==1){
        uint32_t id=r32(pb+48);
        if(id && id!=2)return -120;
        if(name && (!range(c,name,1) || mac_cpu_ram(c)[name]))return -50;
        w16(pb+22,(uint16_t)VREF);return 0; /* Root never creates a WD. */
    }
    if((int16_t)r16(pb+26)!=0)return -51;
    if(!output_name(c,address,52,name,vol_name))return -50;
    w16(pb+22,(uint16_t)VREF);w32(pb+28,0);w16(pb+32,(uint16_t)VREF);w32(pb+48,2);
    put_name(c,name,vol_name);return 0;
}
static int catalog(MacCpu *c,MacSystemVolume *v,uint32_t address,uint8_t *pb) {
    int index=(int16_t)r16(pb+28);uint32_t id=r32(pb+48),name=r32(pb+18);
    int root=index<0;
    if(root && id!=2)return -120;
    if(!root && id && id!=2)return -120;
    if(index>1)return -43;
    if(!root && index==0){
        if(!name || !range(c,name,1) || !range(c,name,1u+mac_cpu_ram(c)[name]))return -50;
        if(!equal_name(mac_cpu_ram(c)+name,"System"))return -43;
    }
    const char *label=root?vol_name:"System";
    size_t size=root?104:108;
    if(!output_name(c,address,size,name,label))return -50;
    uint8_t out[108]={0};memcpy(out,pb,size);memset(out+30,0,size-30);
    out[30]=root?0x10:1; /* Authored file locked; root directory. */
    w32(out+48,root?2:16);
    if(root)w16(out+52,1);
    else {
        w16(out+24,FIRST_REF); /* Pinned System resource fork is always open. */
        for(unsigned i=0;i<OPEN_COUNT;i++)if(v->files[i].open)
            out[30]|=(uint8_t)(0x80|(v->files[i].resource?4:8));
        w32(out+64,FORK_BYTES);w32(out+68,512);w32(out+104,512);
    }
    w32(out+72,v->created);w32(out+76,v->created);w32(out+100,root?1:2);
    memcpy(pb,out,size);put_name(c,name,label);return 0;
}
static int volume_info(MacRuntime *rt,MacSystemVolume *v,uint32_t address,uint8_t *pb) {
    MacCpu *c=rt->cpu;
    uint32_t name=r32(pb+18);
    int index=(int16_t)r16(pb+28);
    if(index>0 && index!=2)return -35;
    if(index<0 && name){
        if(!range(c,name,1) || !range(c,name,1u+mac_cpu_ram(c)[name]))return -50;
        if(!equal_name(mac_cpu_ram(c)+name,vol_name))return -35;
    }
    if(!output_name(c,address,122,name,vol_name))return -50;
    uint8_t out[122];memcpy(out,pb,sizeof out);memset(out+30,0,92);
    w16(out+22,(uint16_t)VREF);w32(out+30,v->created);w32(out+34,v->created);
    w16(out+38,0x8000);w16(out+40,1);w16(out+46,1);w32(out+48,512);w32(out+52,512);
    w32(out+58,17);w16(out+64,0x4244);w16(out+66,(uint16_t)mac_drive_number(rt,2));w32(out+82,1);w32(out+86,0);
    w32(out+90,2); /* Authored System file is actually in volume root. */
    memcpy(pb,out,sizeof out);put_name(c,name,vol_name);return 0;
}
int mac_system_volume_service(MacRuntime *rt,MacSystemVolume *v,uint16_t opcode) {
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu))return -1;
    if(!v || opcode>=0xa800)return 0;
    MacCpu *c=rt->cpu;unsigned call=opcode&0xff,selector=mac_cpu_get(c,MAC_D0)&0xffff;
    int hfs=call==0x60 && (opcode&0x200);
    int ref_call=call==1 || call==2 || call==3 || call==0x11 || call==0x12 || call==0x18 || call==0x44;
    int open_call=call==0 || call==0x0a;
    int vol_call=call==7 && (opcode&0x200);
    if(!ref_call && !open_call && !vol_call && !(hfs&&(selector==1||selector==2||selector==7||selector==8||selector==9)))return 0;
    uint32_t p=mac_cpu_get(c,MAC_A0);
    if((p&1) || !range(c,p,30)){mac_cpu_halt(c,"Invalid system-volume parameter block.");return -1;}
    uint8_t *pb=mac_cpu_ram(c)+p;
    int volume=(int16_t)r16(pb+22),ref=(int16_t)r16(pb+24),index=(int16_t)r16(pb+28);
    int ours=ref_call?(ref>=FIRST_REF&&ref<FIRST_REF+OPEN_COUNT):volume==VREF;
    if(hfs&&selector==8&&index==0)ours=volume==VREF || (ref>=FIRST_REF&&ref<FIRST_REF+OPEN_COUNT);
    if(vol_call&&index==2)ours=1;
    if(vol_call&&index<0){
        uint32_t name=r32(pb+18);
        if(name && range(c,name,1) && range(c,name,1u+mac_cpu_ram(c)[name]) &&
           equal_name(mac_cpu_ram(c)+name,vol_name))ours=1;
    }
    if(hfs&&(selector==1||selector==2||selector==7)&&mac_files_wd_ours(rt,selector,mac_cpu_read(c,p+48,4),volume))ours=0;   /* a made folder's working directory is the files service's */
    if(hfs&&selector==9&&index==0&&ours){   /* a named item other than the authored System file belongs to the flat save namespace (mac_files_dircreate.c) */
        uint32_t nm=r32(pb+18);
        if(nm && range(c,nm,1) && range(c,nm,1u+mac_cpu_ram(c)[nm]) && !equal_name(mac_cpu_ram(c)+nm,"System"))ours=0;
    }
    if(!ours)return 0;
    size_t size=vol_call?122:hfs?(selector==8?62:selector==9?(index<0?104:108):52):(open_call&&(opcode&0x200)?52:call==0x11?32:50);
    if(!range(c,p,size)){mac_cpu_halt(c,"Truncated system-volume parameter block.");return -1;}
    if(opcode&0x400)return done(c,pb,-50);
    int error;
    if(ref_call)error=file_control(c,v,pb,call);
    else if(open_call)error=open_file(c,v,pb,opcode);
    else if(vol_call)error=volume_info(rt,v,p,pb);
    else if(selector==8)error=fcb_info(c,v,p,pb);
    else if(selector==9)error=catalog(c,v,p,pb);
    else error=wd_info(c,p,pb,selector);
    return done(c,pb,error);
}
