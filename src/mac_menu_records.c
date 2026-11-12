#include <stdio.h>
#include "mac_menu_int.h"
#include "mac_mdef.h"
#include <string.h>
/* Public MenuInfo/AppendMenu/InsertMenu contracts, authored implementation:
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-187.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-144.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-120.html
 * A menu whose menuProc (long at offset 6) is a live, locked MDEF resource handle runs that 68K code (mac_mdef.c); the standard definition is native. */
#define MENU_TYPE UINT32_C(0x4d454e55)
static int valid(MacRuntime*r,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(r->cpu);return p && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static MacHandle *owned_x(MacRuntime*r,uint32_t h,int raw){
    if((h&1)||!valid(r,h,4))return NULL;
    for(size_t i=0;i<r->handle_count;i++){
        MacHandle*m=&r->handles[i];
        if(m->handle==h && (m->resource==0 || (m->resource==2 && m->type==MENU_TYPE) || (raw && m->resource==1 && m->type==MENU_TYPE)) && !m->loaded_code && !(m->pointer&1) &&
           valid(r,m->pointer,m->size) && rd(r,h,4)==m->pointer)return m;
    }return NULL;
}
static MacHandle *owned(MacRuntime*r,uint32_t h){return owned_x(r,h,0);}
/* `raw`: also accept a MENU resource handle straight from GetResource (a MENU resource has the layout of a menu record, so a real Mac lets a program use it
 * as a MenuHandle; only counting items is allowed through this path). */
static MacHandle *menu_record_x(MacRuntime*r,uint32_t h,unsigned *items,int raw){
    MacHandle*m=owned_x(r,h,raw);if(!m || m->type!=MENU_TYPE || m->size<16)return NULL;
    uint32_t proc=rd(r,m->pointer+6,4);   /* 0: standard; a live MDEF handle; or, in a raw MENU resource, the MDEF id in the high half */
    if(proc && !(m->resource==1 ? !(proc&0xffff) : mac_mdef_code(r,proc)))return NULL;
    const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];unsigned count=0;
    if(p>=m->size)return NULL;
    while(b[p]){size_t n=b[p]+5u;if(n>m->size-p || ++count>31)return NULL;p+=n;if(p>=m->size)return NULL;}
    if(p+1!=m->size)return NULL;
    if(items)*items=count;return m;
}
MacHandle *mac_menu_record(MacRuntime*r,uint32_t h,unsigned *items){return menu_record_x(r,h,items,0);}
/* A menu list handle: a count followed by that many owned, distinct menu handles (our private representation). */
static MacHandle *list_at(MacRuntime*r,uint32_t h,unsigned *count){
    MacHandle*m=owned(r,h);if(!m || m->type || m->size<2)return NULL;
    unsigned n=rd(r,m->pointer,2);if(n>32 || m->size!=2+4*n)return NULL;
    for(unsigned i=0;i<n;i++){
        uint32_t x=rd(r,m->pointer+2+4*i,4);if(!mac_menu_record(r,x,NULL))return NULL;
        for(unsigned j=0;j<i;j++)if(rd(r,m->pointer+2+4*j,4)==x)return NULL;
    }
    if(count)*count=n;return m;
}
MacHandle *mac_menu_list(MacRuntime*r,unsigned *count){
    if(!(r->manager_flags&4) || rd(r,0xa1c,4)!=r->menu_bar)return NULL;
    return list_at(r,r->menu_bar,count);
}
static int space(MacRuntime*r,unsigned n){
    uint32_t end=r->heap_limit;if(r->heap_high && r->heap_high<end)end=r->heap_high;
    if(end>mac_cpu_ram_size(r->cpu))end=(uint32_t)mac_cpu_ram_size(r->cpu);
    return r->heap_next>=0x1000 && !(r->heap_next&3) && r->heap_next<=end && n<=end-r->heap_next;
}
static int resize(MacRuntime*r,MacHandle*m,const uint8_t*b,unsigned n){
    if(m->state&0x80){wr(r,0x220,2,(uint16_t)-117);return 0;}
    if(mac_heap_owns(r,m->handle)){
        /* Protected-heap handle: let the heap resize and relocate it, so its master pointer and bookkeeping stay
         * consistent (rewriting the pointer here would desynchronize them from the heap). */
        uint32_t a0=mac_cpu_get(r->cpu,MAC_A0),d0=mac_cpu_get(r->cpu,MAC_D0);
        mac_cpu_set(r->cpu,MAC_A0,m->handle);mac_cpu_set(r->cpu,MAC_D0,n);
        mac_memory_service(r,0xa024);
        int16_t err=(int16_t)rd(r,0x220,2);
        mac_cpu_set(r->cpu,MAC_A0,a0);mac_cpu_set(r->cpu,MAC_D0,d0);
        if(err)return 0;
        memcpy(mac_cpu_ram(r->cpu)+rd(r,m->handle,4),b,n);
        return 1;
    }
    if(!space(r,(n+3u)&~3u)){wr(r,0x220,2,(uint16_t)-108);return 0;}
    uint32_t p=mac_runtime_alloc(r,n);memcpy(mac_cpu_ram(r->cpu)+p,b,n);
    m->pointer=p;m->size=n;wr(r,m->handle,4,p);wr(r,0x220,2,0);return 1;
}
/* `custom`: the menu has a definition procedure, which owns the sizes and the look; only the item list is checked then. */
static int dimensions(const uint8_t*b,size_t size,unsigned *width,unsigned *height,int custom){
    if(size<16 || (!custom && (b[6] || b[7] || b[8] || b[9])))return 0;
    size_t p=15u+b[14];unsigned count=0,w=0;
    if(p>=size)return 0;
    while(b[p]){
        unsigned n=b[p];if(n+5u>size-p || ++count>31)return 0;
        if(!custom && (b[p+n+1] || b[p+n+2]==28))return 0; /* icons unsupported; command 27 = submenu, ID in the mark byte */
        unsigned item=n*8+32;if(item>w)w=item;p+=n+5u;if(p>=size)return 0;
    }
    if(p+1!=size)return 0;
    *width=w;*height=count*16;return 1;
}
static int get_menu_core(MacRuntime*r,int16_t id,uint32_t *out,int *custom_out){
    MacResource resource,colors;*out=0;*custom_out=0;
    if(mac_resource_find(&r->fork,MENU_TYPE,id,&resource)){
        wr(r,0xa60,2,(uint16_t)-192);return 1;
    }
    /* A menu colour table ('mctb') with the same ID is not consulted: menus are drawn in our own black and white (see mac_menu.c), so the
     * colours it names do not show. The menu itself is read as usual. */
    (void)colors;
    const uint8_t *data=resource.data;size_t size=resource.size;MacHandle *m=NULL;
    for(size_t i=0;i<r->handle_count;i++){
        MacHandle*h=&r->handles[i];if(h->resource && h->type==MENU_TYPE && h->id==id){
            if(!valid(r,h->handle,4))return fail(r,"GetMenu resource master pointer is invalid.");
            uint32_t p=rd(r,h->handle,4);
            if(p){if(p!=h->pointer || !valid(r,p,h->size))return fail(r,"GetMenu loaded resource is invalid.");data=mac_cpu_ram(r->cpu)+p;size=h->size;}
            m=h;break;
        }
    }
    unsigned width,height;
    /* The menu names a menu definition procedure (MDEF id in bytes 6-7), or was made from one already (the long at 6 is its live handle). */
    uint32_t mdef=0;int custom=0;
    if(size>=10){
        uint32_t proc=(uint32_t)data[6]<<24|(uint32_t)data[7]<<16|(uint32_t)data[8]<<8|data[9];
        if(proc && m && m->resource==2 && mac_mdef_code(r,proc)){custom=1;mdef=proc;}
        else if(proc){
            if(proc&0xffff)return fail(r,"GetMenu has malformed items or unsupported menu definitions/icons.");
            unsigned mid=proc>>16;custom=1;mdef=mac_mdef_load(r,mid);
            if(!mdef){
                char why[160];snprintf(why,sizeof why,"GetMenu: MENU %d names MDEF %u, but the program has no usable MDEF resource with that ID.",id,mid);
                return fail(r,why);
            }
        }
    }
    if(!dimensions(data,size,&width,&height,custom))return fail(r,"GetMenu has malformed items or unsupported menu definitions/icons.");
    uint32_t h=0;
    if(m && !rd(r,m->handle,4)){
        /* A purged resource retains its master handle, not a valid data
         * pointer. Reload validated backing before publishing menu fields. */
        if(size<=UINT32_MAX && space(r,((unsigned)size+3u)&~3u)){
            uint32_t p=mac_runtime_alloc(r,(uint32_t)size);
            memcpy(mac_cpu_ram(r->cpu)+p,data,size);
            m->pointer=p;m->size=(uint32_t)size;wr(r,m->handle,4,p);h=m->handle;
        }
    }else if(m)h=m->handle;
    else if(size<=UINT32_MAX-7u && space(r,4+(((unsigned)size+3u)&~3u)))
        h=mac_runtime_resource(r,MENU_TYPE,id);
    if(h){
        for(size_t i=0;i<r->handle_count;i++)if(r->handles[i].handle==h){m=&r->handles[i];break;}
        if(!m)return fail(r,"GetMenu lost resource ownership.");
        m->resource=2;
        if(custom)wr(r,m->pointer+6,4,mdef);                       /* sized by the MDEF's size message, which the caller sends */
        else {wr(r,m->pointer+2,2,width);wr(r,m->pointer+4,2,height);}
    }
    wr(r,0xa60,2,h?0:(uint16_t)-108);wr(r,0x220,2,h?0:(uint16_t)-108);
    *out=h;*custom_out=h&&custom;return 1;
}
static int get_menu(MacRuntime*r,uint32_t sp){
    uint32_t h;int custom,rc=get_menu_core(r,(int16_t)rd(r,sp,2),&h,&custom);if(rc<0)return rc;
    if(custom)return mac_mdef_size_menus(r,&h,1,sp,2,sp+2,h);   /* the result is stored when the size message returns */
    wr(r,sp+2,4,h);mac_cpu_set(r->cpu,MAC_A7,sp+2);return 1;
}
/* GetNewMBar(menuBarID): MBAR is a count followed by that many menu IDs. Each menu is loaded as GetMenu does and the
 * result is a new menu list handle (not installed: SetMenuBar does that). Duplicate IDs collapse to one entry. */
static int get_new_mbar(MacRuntime*r,uint32_t sp){
    int16_t id=(int16_t)rd(r,sp,2),err=0;uint32_t out=0,cs[32];unsigned ncustom=0;MacResource bar;
    if(mac_resource_find(&r->fork,MAC_RESOURCE_TYPE('M','B','A','R'),id,&bar))err=-192;
    else {
        unsigned n=bar.size>=2?(unsigned)(bar.data[0]<<8|bar.data[1]):0;
        if(bar.size<2 || bar.size<2+2u*n)err=-199;
        else if(n>32)return fail(r,"GetNewMBar exceeds the bounded 32-menu capacity.");
        else {
            uint32_t hs[32];unsigned count=0;
            for(unsigned i=0;i<n && !err;i++){
                int16_t mid=(int16_t)(bar.data[2+2*i]<<8|bar.data[3+2*i]);uint32_t h;
                int custom,rc=get_menu_core(r,mid,&h,&custom);if(rc<0)return rc;
                if(!h){err=(int16_t)rd(r,0xa60,2);if(!err)err=-108;break;}
                unsigned k=0;while(k<count && hs[k]!=h)k++;
                if(k==count){hs[count++]=h;if(custom)cs[ncustom++]=h;}
            }
            if(!err){
                uint32_t a0=mac_cpu_get(r->cpu,MAC_A0),d0=mac_cpu_get(r->cpu,MAC_D0);
                mac_cpu_set(r->cpu,MAC_D0,2+4*count);mac_memory_service(r,0xa122); /* NewHandle */
                uint32_t lh=mac_cpu_get(r->cpu,MAC_A0);int16_t e=(int16_t)rd(r,0x220,2);
                mac_cpu_set(r->cpu,MAC_A0,a0);mac_cpu_set(r->cpu,MAC_D0,d0);
                if(e || !lh)err=-108;
                else {
                    uint32_t p=rd(r,lh,4);wr(r,p,2,count);
                    for(unsigned i=0;i<count;i++)wr(r,p+2+4*i,4,hs[i]);
                    out=lh;
                }
            }
        }
    }
    wr(r,0xa60,2,(uint16_t)err);wr(r,0x220,2,(uint16_t)err);
    if(out && ncustom)return mac_mdef_size_menus(r,cs,ncustom,sp,2,sp+2,out);   /* custom menus are sized before the list is delivered */
    wr(r,sp+2,4,out);mac_cpu_set(r->cpu,MAC_A7,sp+2);return 1;
}
static int new_menu(MacRuntime*r,uint32_t sp){
    uint32_t text=rd(r,sp,4);unsigned id=rd(r,sp+4,2);
    if(!valid(r,text,1)||!valid(r,text,1+rd(r,text,1)))return fail(r,"NewMenu title is invalid.");
    uint8_t title[256];unsigned n=rd(r,text,1);memcpy(title,mac_cpu_ram(r->cpu)+text,n+1);
    unsigned slot=0;while(slot<MAC_RUNTIME_HANDLES&&(r->handles[slot].handle||r->handles[slot].pointer))slot++;
    uint32_t h=0;
    if(slot<MAC_RUNTIME_HANDLES && space(r,4+((n+19)&~3u))){
        h=mac_runtime_alloc(r,4);uint32_t p=mac_runtime_alloc(r,16+n);
        wr(r,h,4,p);r->handles[slot]=(MacHandle){.handle=h,.pointer=p,.size=16+n,.type=MENU_TYPE};
        if(r->handle_count<=slot)r->handle_count=slot+1;
        wr(r,p,2,id);wr(r,p+10,4,UINT32_MAX);memcpy(mac_cpu_ram(r->cpu)+p+14,title,n+1);
    }
    wr(r,0x220,2,h?0:(uint16_t)-108);wr(r,sp+6,4,h);mac_cpu_set(r->cpu,MAC_A7,sp+6);return 1;
}
/* AppendMenu(menu, data) and InsertMenuItem(menu, itemString, afterItem; `insert`): the items the string describes are added at the end, or after item
 * afterItem (0: before the first; past the end: appended). Several items in one InsertMenuItem string are inserted in the reverse of their order in the
 * string (from memory of the call's description). The enable flags of the items that move up move with them. */
static int append(MacRuntime*r,uint32_t sp,int insert){
    unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+(insert?6:4),4),&count);uint32_t p=rd(r,sp+(insert?2:0),4);
    if(!m || !valid(r,p,1) || !valid(r,p,1+rd(r,p,1)))return fail(r,"AppendMenu/InsertMenuItem requires a valid owned menu and string.");
    uint8_t input[256],out[4096],fresh[2048];unsigned length=rd(r,p,1);memcpy(input,mac_cpu_ram(r->cpu)+p,length+1);
    if(m->size>sizeof out)return fail(r,"Menu record exceeds bounded storage.");
    const uint8_t*old=mac_cpu_ram(r->cpu)+m->pointer;unsigned used=m->size-1,fn=0,added=0,starts[32];
    uint32_t flags=rd(r,m->pointer+10,4),enabled=0;
    int afterv=insert?(int16_t)rd(r,sp,2):(int)count;unsigned after=afterv<0?0:(unsigned)afterv>count?count:(unsigned)afterv;
    for(unsigned i=1;i<=length;){
        uint8_t text[256],key=0,mark=0,style=0;unsigned n=0;int disabled=0;
        while(i<=length && input[i]!=';' && input[i]!=13){
            unsigned c=input[i++];
            if(c=='('){disabled=1;continue;}
            if(c=='^')return fail(r,"Menu icons are unsupported.");
            if(c=='/' || c=='!'){
                if(i>length)return fail(r,"AppendMenu metacharacter lacks a value.");
                unsigned v=input[i++];if(c=='/') {if(v<32)return fail(r,"Hierarchical/script menu commands are unsupported.");key=(uint8_t)v;}else mark=(uint8_t)v;
                continue;
            }
            if(c=='<'){
                if(i>length)return fail(r,"AppendMenu style lacks a value.");
                while(i<=length){const char *styles="BIUOS";const char*q=strchr(styles,input[i]);if(!q)break;style|=(uint8_t)(1u<<(q-styles));i++;}
                continue;
            }
            text[n++]=(uint8_t)c;
        }
        if(i<=length)i++;
        if(!n)return fail(r,"Empty AppendMenu items are unsupported.");
        if(count+added>=31 || used+fn+n+6>sizeof out || fn+n+5>sizeof fresh)return fail(r,"Menu item capacity exceeded.");
        if(!disabled)enabled|=1u<<added;
        starts[added++]=fn;
        fresh[fn++]=(uint8_t)n;memcpy(fresh+fn,text,n);fn+=n;
        fresh[fn++]=0;fresh[fn++]=key;fresh[fn++]=mark;fresh[fn++]=style;
    }
    unsigned pos=15u+old[14];for(unsigned i=0;i<after;i++)pos+=old[pos]+5u;
    memcpy(out,old,pos);unsigned at=pos;
    uint32_t low=flags&((2u<<after)-1u),high=after>=31?0:(flags>>(after+1))<<(after+1+added),mid=0;
    for(unsigned k=0;k<added;k++){
        unsigned src=insert?added-1-k:k,end=src+1<added?starts[src+1]:fn;
        memcpy(out+at,fresh+starts[src],end-starts[src]);at+=end-starts[src];
        if(enabled&(1u<<src))mid|=1u<<(after+1+k);
    }
    memcpy(out+at,old+pos,used-pos);used=at+(used-pos);flags=low|mid|high;
    out[used++]=0;out[10]=(uint8_t)(flags>>24);out[11]=(uint8_t)(flags>>16);out[12]=(uint8_t)(flags>>8);out[13]=(uint8_t)flags;
    unsigned width,height;int custom=rd(r,m->pointer+6,4)!=0;   /* a custom menu keeps its sizes until the MDEF is next asked (CalcMenuSize, MenuSelect) */
    if(!dimensions(out,used,&width,&height,custom))return fail(r,"AppendMenu produced unsupported item data.");
    if(!custom){out[2]=(uint8_t)(width>>8);out[3]=(uint8_t)width;out[4]=(uint8_t)(height>>8);out[5]=(uint8_t)height;}
    if(length)(void)resize(r,m,out,used);
    mac_cpu_set(r->cpu,MAC_A7,sp+(insert?10:8));return 1;
}
/* SetItem(menu, item, text): replace one item's text literally (no metacharacter parsing, unlike AppendMenu); mark, key,
 * style and enable bits are kept. The record is rebuilt so the item widths in the header stay true. An out-of-range item
 * is ignored. Empty text cannot be stored in this record encoding (a zero length byte ends the item list), so it halts. */
static int set_item(MacRuntime*r,uint32_t sp){
    unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+6,4),&count);uint32_t text=rd(r,sp,4);unsigned item=rd(r,sp+4,2);
    if(!m || !valid(r,text,1) || !valid(r,text,1+rd(r,text,1)))return fail(r,"SetItem requires a valid owned menu and string.");
    unsigned n=rd(r,text,1);
    if(!n)return fail(r,"SetItem empty item text is unsupported.");
    if(item && item<=count){
        uint8_t out[4096];const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];
        for(unsigned i=1;i<item;i++)p+=b[p]+5u;
        size_t old=b[p],total=m->size-old+n;
        if(total>sizeof out)return fail(r,"Menu record exceeds bounded storage.");
        memcpy(out,b,p);out[p]=(uint8_t)n;memcpy(out+p+1,mac_cpu_ram(r->cpu)+text+1,n);
        memcpy(out+p+1+n,b+p+1+old,m->size-(p+1+old));
        unsigned width,height;int custom=rd(r,m->pointer+6,4)!=0;
        if(!dimensions(out,total,&width,&height,custom))return fail(r,"SetItem produced unsupported item data.");
        if(!custom){out[2]=(uint8_t)(width>>8);out[3]=(uint8_t)width;out[4]=(uint8_t)(height>>8);out[5]=(uint8_t)height;}
        (void)resize(r,m,out,(unsigned)total);
    }
    mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
}
/* A MENU resource straight from GetResource becomes an ordinary menu the first time InsertMenu is given it (as GetMenu would have made it: published sizes, owned). */
static void adopt_raw(MacRuntime*r,uint32_t h){
    MacHandle*m=owned_x(r,h,1);unsigned w,hh;
    if(!m || m->resource!=1 || m->type!=MENU_TYPE || rd(r,m->pointer+6,4) || !dimensions(mac_cpu_ram(r->cpu)+m->pointer,m->size,&w,&hh,0))return;   /* a menu naming an MDEF only becomes a menu through GetMenu */
    m->resource=2;wr(r,m->pointer+2,2,w);wr(r,m->pointer+4,2,hh);
}
int mac_menu_records_service(MacRuntime*r,uint16_t op){
    unsigned args=0,result=0;
    switch(op){case 0xa931:args=6;result=4;break;case 0xa947:case 0xa941:case 0xa946:case 0xa84e:case 0xa943:case 0xa93f:case 0xa826:args=10;break;case 0xa933:case 0xa945:case 0xa94d:case 0xa942:case 0xa944:case 0xa84f:case 0xa940:args=8;break;
    case 0xa939:case 0xa93a:case 0xa935:case 0xa952:args=6;break;case 0xa932:case 0xa93c:case 0xa948:args=4;break;case 0xa936:args=2;break;
    case 0xa934:args=0;break;
    case 0xa9bf:case 0xa9c0:args=2;result=4;break;case 0xaa64:args=4;result=4;break;case 0xa950:args=4;result=2;break;case 0xa949:args=2;result=4;break;default:return 0;}
    if(!r || !r->cpu || mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if((sp&1)||!valid(r,sp,args+result)||!valid(r,0xa1c,0x800))return fail(r,"Menu call stack or low memory is invalid.");
    if(op==0xaa64){wr(r,sp+4,4,0);mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;}   /* GetMCEntry(menuID, item): no menu colour tables exist here, so there is never an entry */
    if(op==0xa931)return new_menu(r,sp);
    if(op==0xa9bf)return get_menu(r,sp);
    if(op==0xa9c0)return get_new_mbar(r,sp);
    if(op==0xa933)return append(r,sp,0);
    if(op==0xa826)return append(r,sp,1);
    if(op==0xa947)return set_item(r,sp);
    if(op==0xa948){   /* CalcMenuSize(menu): menuWidth and menuHeight from the items, the same sizes GetMenu publishes (8x8 text) */
        uint32_t mh=rd(r,sp,4);MacHandle*m=menu_record_x(r,mh,NULL,1);unsigned width,height;
        if(!m)return fail(r,"CalcMenuSize menu is invalid.");
        if(rd(r,m->pointer+6,4)){   /* a custom menu: the definition procedure answers the size message */
            if(!mac_mdef_of_menu(r,m))return fail(r,"CalcMenuSize: the menu names a menu definition that GetMenu has not loaded.");
            return mac_mdef_size_menus(r,&mh,1,sp,4,0,0);
        }
        if(!dimensions(mac_cpu_ram(r->cpu)+m->pointer,m->size,&width,&height,0))return fail(r,"CalcMenuSize has malformed items or unsupported menu definitions/icons.");
        wr(r,m->pointer+2,2,width);wr(r,m->pointer+4,2,height);
        mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;
    }
    if(op==0xa946){   /* GetItem(menu, item, VAR itemString): the item's text as a Pascal string; an item outside the menu gives an empty string */
        unsigned count;MacHandle*m=menu_record_x(r,rd(r,sp+6,4),&count,1);uint32_t out=rd(r,sp,4);int item=(int16_t)rd(r,sp+4,2);
        if(!m)return fail(r,"GetItem menu is invalid.");
        const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];unsigned n=0;
        if(item>=1 && (unsigned)item<=count){for(int i=1;i<item;i++)p+=b[p]+5u;n=b[p];}
        if(!out||!valid(r,out,1+n))return fail(r,"GetItem destination is invalid.");
        wr(r,out,1,n);if(n)memcpy(mac_cpu_ram(r->cpu)+out+1,b+p+1,n);
        mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
    }
    if(op==0xa942||op==0xa944||op==0xa84f||op==0xa940){   /* SetItemStyle / SetItemMark / SetItemCmd / SetItemIcon (menu, item, byte): the byte is the first of the pushed word; an item outside the menu is ignored */
        unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+4,4),&count);int item=(int16_t)rd(r,sp+2,2);
        if(!m)return fail(r,"SetItemStyle/Mark/Cmd/Icon menu is invalid.");
        if(item>=1 && (unsigned)item<=count){
            const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];
            for(int i=1;i<item;i++)p+=b[p]+5u;
            wr(r,m->pointer+p+b[p]+(op==0xa942?4:op==0xa944?3:op==0xa84f?2:1),1,rd(r,sp,1));
        }
        mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
    }
    if(op==0xa93f){   /* GetItemIcon(menu, item, VAR iconIndex: INTEGER): the icon byte as an integer (the 2-byte size is from memory of Inside Macintosh) */
        unsigned count;MacHandle*m=menu_record_x(r,rd(r,sp+6,4),&count,1);uint32_t out=rd(r,sp,4);int item=(int16_t)rd(r,sp+4,2);
        if(!m)return fail(r,"GetItemIcon menu is invalid.");
        if(!out||(out&1)||!valid(r,out,2))return fail(r,"GetItemIcon destination is invalid.");
        if(item>=1 && (unsigned)item<=count){
            const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];
            for(int i=1;i<item;i++)p+=b[p]+5u;
            wr(r,out,2,b[p+b[p]+1]);
        }
        mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
    }
    if(op==0xa941||op==0xa84e||op==0xa943){   /* GetItemStyle / GetItemCmd / GetItemMark (menu, item, VAR byte): that byte of the item; an item outside the menu leaves it alone. GetItemCmd and GetItemMark store a 16-bit value (the character in the low byte), as the C form of the call with a short pointer does; GetItemStyle stores one byte */
        unsigned count;MacHandle*m=menu_record_x(r,rd(r,sp+6,4),&count,1);uint32_t out=rd(r,sp,4);int item=(int16_t)rd(r,sp+4,2);
        if(!m)return fail(r,"GetItemStyle/Cmd/Mark menu is invalid.");
        unsigned width=op==0xa941?1:2;
        if(!out||!valid(r,out,width))return fail(r,"GetItemStyle destination is invalid.");
        if(item>=1 && (unsigned)item<=count){
            const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];
            for(int i=1;i<item;i++)p+=b[p]+5u;
            wr(r,out,width,b[p+b[p]+(op==0xa941?4:op==0xa943?3:2)]);
        }
        mac_cpu_set(r->cpu,MAC_A7,sp+10);return 1;
    }
    if(op==0xa94d){
        if(!mac_menu_record(r,rd(r,sp+4,4),NULL))return fail(r,"AppendResMenu menu is invalid.");
        uint32_t type=rd(r,sp,4);
        /* Only mounted forks are application plus the authored empty System.
         * Do not invent desk accessories or claim nonempty enumeration support. */
        for(size_t i=0;i<mac_resource_count(&r->fork);i++){
            MacResource resource;
            if(!mac_resource_at(&r->fork,i,&resource) && resource.type==type && resource.name_size &&
               resource.name[0]!='.' && resource.name[0]!='%')
                return fail(r,"AppendResMenu nonempty resource enumeration is unsupported.");
        }
        mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
    }
    if(op==0xa945){
        unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+4,4),&count);
        if(!m)return fail(r,"CheckItem menu is invalid.");
        unsigned item=rd(r,sp+2,2);
        if(item && item<=count){uint32_t p=m->pointer+15+rd(r,m->pointer+14,1);
            for(unsigned i=1;i<item;i++)p+=rd(r,p,1)+5;
            wr(r,p+rd(r,p,1)+3,1,rd(r,sp,1)?0x12:0);}
        mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
    }
    if(op==0xa939 || op==0xa93a){
        unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+2,4),&count);
        if(!m)return fail(r,"EnableItem/DisableItem menu is invalid.");
        unsigned item=rd(r,sp,2);
        if(item<=count && item<32){uint32_t flags=rd(r,m->pointer+10,4),mask=1u<<item;
            wr(r,m->pointer+10,4,op==0xa939?flags|mask:flags&~mask);}
        mac_cpu_set(r->cpu,MAC_A7,sp+6);return 1;
    }
    if(op==0xa952){   /* DeleteMenuItem(menu, item): the item goes, the later items move up one (their enable bits with them); an item outside the menu is ignored */
        unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+2,4),&count);int item=(int16_t)rd(r,sp,2);
        if(!m)return fail(r,"DeleteMenuItem menu is invalid.");
        if(item>=1 && (unsigned)item<=count){
            uint8_t out[4096];const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];
            for(int i=1;i<item;i++)p+=b[p]+5u;
            size_t len=b[p]+5u;
            if(m->size>sizeof out)return fail(r,"Menu record exceeds bounded storage.");
            memcpy(out,b,p);memcpy(out+p,b+p+len,m->size-p-len);
            unsigned total=(unsigned)(m->size-len);
            uint32_t flags=rd(r,m->pointer+10,4);
            uint32_t low=(uint32_t)((uint64_t)flags&((2ull<<(item-1))-1)),high=item<31?(flags>>(item+1))<<item:0,nf=low|high;
            out[10]=(uint8_t)(nf>>24);out[11]=(uint8_t)(nf>>16);out[12]=(uint8_t)(nf>>8);out[13]=(uint8_t)nf;
            unsigned width,height;int custom=rd(r,m->pointer+6,4)!=0;
            if(!dimensions(out,total,&width,&height,custom))return fail(r,"DeleteMenuItem produced unsupported item data.");
            if(!custom){out[2]=(uint8_t)(width>>8);out[3]=(uint8_t)width;out[4]=(uint8_t)(height>>8);out[5]=(uint8_t)height;}
            (void)resize(r,m,out,total);
        }
        mac_cpu_set(r->cpu,MAC_A7,sp+6);return 1;
    }
    if(op==0xa950){unsigned n; if(!menu_record_x(r,rd(r,sp,4),&n,1))return fail(r,"CountMItems menu is invalid.");wr(r,sp+4,2,n);mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;}
    if(op==0xa93c){ /* SetMenuBar: the installed list takes the menus of the given list (a copy, so the caller may dispose its handle) */
        unsigned sc,dc;MacHandle*src=list_at(r,rd(r,sp,4),&sc);
        if(!src)return fail(r,"SetMenuBar requires a valid owned menu list.");
        MacHandle*dst=mac_menu_list(r,&dc);if(!dst)return fail(r,"Menu list is invalid or uninitialized.");
        if(src!=dst){uint8_t b[130];memcpy(b,mac_cpu_ram(r->cpu)+src->pointer,src->size);(void)resize(r,dst,b,src->size);}
        mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;
    }
    unsigned count;MacHandle*list=mac_menu_list(r,&count);if(!list)return fail(r,"Menu list is invalid or uninitialized.");
    uint32_t hs[32];for(unsigned i=0;i<count;i++)hs[i]=rd(r,list->pointer+2+4*i,4);
    if(op==0xa934){count=0;} /* ClearMenuBar: empty the installed list; the menus themselves stay valid */
    else if(op==0xa949 || op==0xa936){
        unsigned id=rd(r,sp,2),i=0;while(i<count && rd(r,mac_menu_record(r,hs[i],NULL)->pointer,2)!=id)i++;
        unsigned hi=0;while(i==count && hi<r->hier_count && (!mac_menu_record(r,r->hier_menus[hi],NULL) ||
            rd(r,mac_menu_record(r,r->hier_menus[hi],NULL)->pointer,2)!=id))hi++;
        if(op==0xa949)wr(r,sp+2,4,i<count?hs[i]:hi<r->hier_count?r->hier_menus[hi]:0);
        else if(i<count){memmove(hs+i,hs+i+1,(count-i-1)*sizeof(*hs));count--;}
        else if(hi<r->hier_count){memmove(r->hier_menus+hi,r->hier_menus+hi+1,(r->hier_count-hi-1)*sizeof(*r->hier_menus));r->hier_count--;}
    }else if(op==0xa932){
        uint32_t h=rd(r,sp,4);MacHandle*m=mac_menu_record(r,h,NULL);if(!m)return fail(r,"DisposeMenu menu is invalid.");
        for(unsigned i=0;i<count;i++)if(hs[i]==h)return fail(r,"DeleteMenu must precede DisposeMenu.");
        for(unsigned i=0;i<r->hier_count;i++)if(r->hier_menus[i]==h)return fail(r,"DeleteMenu must precede DisposeMenu.");
        wr(r,h,4,0);memset(m,0,sizeof *m);
    }else {
        int before=(int16_t)rd(r,sp,2);uint32_t h=rd(r,sp+2,4);adopt_raw(r,h);MacHandle*m=mac_menu_record(r,h,NULL);
        if(!m)return fail(r,"InsertMenu requires an owned menu.");
        if(before==-1){ /* hierarchical (submenu) list: registered by ID, kept apart from the bar and never drawn there */
            unsigned mid=rd(r,m->pointer,2);int dup=0;
            for(unsigned i=0;i<count;i++)if(rd(r,mac_menu_record(r,hs[i],NULL)->pointer,2)==mid)dup=1;
            for(unsigned i=0;i<r->hier_count;i++){MacHandle*x=mac_menu_record(r,r->hier_menus[i],NULL);if(x && rd(r,x->pointer,2)==mid)dup=1;}
            if(!dup){
                if(r->hier_count==32)return fail(r,"InsertMenu exceeds the bounded 32-submenu capacity.");
                r->hier_menus[r->hier_count++]=h;
            }
            mac_cpu_set(r->cpu,MAC_A7,sp+args);return 1;
        }
        unsigned index=count;int found=0;
        for(unsigned i=0;i<count;i++){
            MacHandle*x=mac_menu_record(r,hs[i],NULL);
            if(rd(r,x->pointer,2)==rd(r,m->pointer,2))found=1;
            if((int16_t)rd(r,x->pointer,2)==before)index=i;
        }
        if(found){mac_cpu_set(r->cpu,MAC_A7,sp+6);return 1;}
        if(count==32)return fail(r,"InsertMenu exceeds the bounded 32-menu capacity.");
        if(!found && count<32){memmove(hs+index+1,hs+index,(count-index)*sizeof(*hs));hs[index]=h;count++;}
    }
    if(op==0xa935 || op==0xa936 || op==0xa934){
        uint8_t b[130]={0};b[1]=(uint8_t)count;
        for(unsigned i=0;i<count;i++){uint32_t h=hs[i];b[2+4*i]=(uint8_t)(h>>24);b[3+4*i]=(uint8_t)(h>>16);b[4+4*i]=(uint8_t)(h>>8);b[5+4*i]=(uint8_t)h;}
        (void)resize(r,list,b,2+4*count);
    }
    mac_cpu_set(r->cpu,MAC_A7,sp+args);return 1;
}
