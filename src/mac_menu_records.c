#include "mac_menu_int.h"
#include <string.h>
/* Public MenuInfo/AppendMenu/InsertMenu contracts, authored implementation:
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-187.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-144.html
 * https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-120.html
 * Native MDEF has no guest executable handle. No interaction is synthesized. */
#define MENU_TYPE UINT32_C(0x4d454e55)
static int valid(MacRuntime*r,uint32_t p,size_t n){size_t z=mac_cpu_ram_size(r->cpu);return p && p<=z && n<=z-p;}
static uint32_t rd(MacRuntime*r,uint32_t p,unsigned n){return mac_cpu_read(r->cpu,p,n);}
static void wr(MacRuntime*r,uint32_t p,unsigned n,uint32_t v){mac_cpu_write(r->cpu,p,n,v);}
static int fail(MacRuntime*r,const char*s){mac_cpu_halt(r->cpu,s);return -1;}
static MacHandle *owned(MacRuntime*r,uint32_t h){
    if((h&1)||!valid(r,h,4))return NULL;
    for(size_t i=0;i<r->handle_count;i++){
        MacHandle*m=&r->handles[i];
        if(m->handle==h && (m->resource==0 || (m->resource==2 && m->type==MENU_TYPE)) && !m->loaded_code && !(m->pointer&1) &&
           valid(r,m->pointer,m->size) && rd(r,h,4)==m->pointer)return m;
    }return NULL;
}
MacHandle *mac_menu_record(MacRuntime*r,uint32_t h,unsigned *items){
    MacHandle*m=owned(r,h);if(!m || m->type!=MENU_TYPE || m->size<16 || rd(r,m->pointer+6,4))return NULL;
    const uint8_t*b=mac_cpu_ram(r->cpu)+m->pointer;size_t p=15u+b[14];unsigned count=0;
    if(p>=m->size)return NULL;
    while(b[p]){size_t n=b[p]+5u;if(n>m->size-p || ++count>31)return NULL;p+=n;if(p>=m->size)return NULL;}
    if(p+1!=m->size)return NULL;
    if(items)*items=count;return m;
}
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
static int dimensions(const uint8_t*b,size_t size,unsigned *width,unsigned *height){
    if(size<16 || b[6] || b[7] || b[8] || b[9])return 0;
    size_t p=15u+b[14];unsigned count=0,w=0;
    if(p>=size)return 0;
    while(b[p]){
        unsigned n=b[p];if(n+5u>size-p || ++count>31)return 0;
        if(b[p+n+1] || b[p+n+2]==28)return 0; /* icons unsupported; command 27 = submenu, ID in the mark byte */
        unsigned item=n*8+32;if(item>w)w=item;p+=n+5u;if(p>=size)return 0;
    }
    if(p+1!=size)return 0;
    *width=w;*height=count*16;return 1;
}
static int get_menu_core(MacRuntime*r,int16_t id,uint32_t *out){
    MacResource resource,colors;*out=0;
    if(mac_resource_find(&r->fork,MENU_TYPE,id,&resource)){
        wr(r,0xa60,2,(uint16_t)-192);return 1;
    }
    if(mac_resource_find(&r->fork,MAC_RESOURCE_TYPE('m','c','t','b'),id,&colors)==0)
        return fail(r,"GetMenu custom color resources are unsupported.");
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
    if(!dimensions(data,size,&width,&height))return fail(r,"GetMenu has malformed items or unsupported menu definitions/icons.");
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
        m->resource=2;wr(r,m->pointer+2,2,width);wr(r,m->pointer+4,2,height);
    }
    wr(r,0xa60,2,h?0:(uint16_t)-108);wr(r,0x220,2,h?0:(uint16_t)-108);
    *out=h;return 1;
}
static int get_menu(MacRuntime*r,uint32_t sp){
    uint32_t h;int rc=get_menu_core(r,(int16_t)rd(r,sp,2),&h);if(rc<0)return rc;
    wr(r,sp+2,4,h);mac_cpu_set(r->cpu,MAC_A7,sp+2);return 1;
}
/* GetNewMBar(menuBarID): MBAR is a count followed by that many menu IDs. Each menu is loaded as GetMenu does and the
 * result is a new menu list handle (not installed: SetMenuBar does that). Duplicate IDs collapse to one entry. */
static int get_new_mbar(MacRuntime*r,uint32_t sp){
    int16_t id=(int16_t)rd(r,sp,2),err=0;uint32_t out=0;MacResource bar;
    if(mac_resource_find(&r->fork,MAC_RESOURCE_TYPE('M','B','A','R'),id,&bar))err=-192;
    else {
        unsigned n=bar.size>=2?(unsigned)(bar.data[0]<<8|bar.data[1]):0;
        if(bar.size<2 || bar.size<2+2u*n)err=-199;
        else if(n>32)return fail(r,"GetNewMBar exceeds the bounded 32-menu capacity.");
        else {
            uint32_t hs[32];unsigned count=0;
            for(unsigned i=0;i<n && !err;i++){
                int16_t mid=(int16_t)(bar.data[2+2*i]<<8|bar.data[3+2*i]);uint32_t h;
                int rc=get_menu_core(r,mid,&h);if(rc<0)return rc;
                if(!h){err=(int16_t)rd(r,0xa60,2);if(!err)err=-108;break;}
                unsigned k=0;while(k<count && hs[k]!=h)k++;
                if(k==count)hs[count++]=h;
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
static int append(MacRuntime*r,uint32_t sp){
    unsigned count;MacHandle*m=mac_menu_record(r,rd(r,sp+4,4),&count);uint32_t p=rd(r,sp,4);
    if(!m || !valid(r,p,1) || !valid(r,p,1+rd(r,p,1)))return fail(r,"AppendMenu requires a valid owned menu and string.");
    uint8_t input[256],out[4096];unsigned length=rd(r,p,1);memcpy(input,mac_cpu_ram(r->cpu)+p,length+1);
    if(m->size>sizeof out)return fail(r,"Menu record exceeds bounded storage.");
    memcpy(out,mac_cpu_ram(r->cpu)+m->pointer,m->size);unsigned used=m->size-1;
    uint32_t flags=rd(r,m->pointer+10,4);
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
        if(++count>31 || used+n+6>sizeof out)return fail(r,"Menu item capacity exceeded.");
        if(disabled)flags&=~(1u<<count);else flags|=1u<<count;
        out[used++]=(uint8_t)n;memcpy(out+used,text,n);used+=n;
        out[used++]=0;out[used++]=key;out[used++]=mark;out[used++]=style;
    }
    out[used++]=0;out[10]=(uint8_t)(flags>>24);out[11]=(uint8_t)(flags>>16);out[12]=(uint8_t)(flags>>8);out[13]=(uint8_t)flags;
    unsigned width,height;if(!dimensions(out,used,&width,&height))return fail(r,"AppendMenu produced unsupported item data.");
    out[2]=(uint8_t)(width>>8);out[3]=(uint8_t)width;out[4]=(uint8_t)(height>>8);out[5]=(uint8_t)height;
    if(length)(void)resize(r,m,out,used);
    mac_cpu_set(r->cpu,MAC_A7,sp+8);return 1;
}
int mac_menu_records_service(MacRuntime*r,uint16_t op){
    unsigned args=0,result=0;
    switch(op){case 0xa931:args=6;result=4;break;case 0xa933:case 0xa945:case 0xa94d:args=8;break;
    case 0xa939:case 0xa93a:case 0xa935:args=6;break;case 0xa932:case 0xa93c:args=4;break;case 0xa936:args=2;break;
    case 0xa934:args=0;break;
    case 0xa9bf:case 0xa9c0:args=2;result=4;break;case 0xa950:args=4;result=2;break;case 0xa949:args=2;result=4;break;default:return 0;}
    if(!r || !r->cpu || mac_cpu_halted(r->cpu))return -1;
    uint32_t sp=mac_cpu_get(r->cpu,MAC_A7);
    if((sp&1)||!valid(r,sp,args+result)||!valid(r,0xa1c,0x800))return fail(r,"Menu call stack or low memory is invalid.");
    if(op==0xa931)return new_menu(r,sp);
    if(op==0xa9bf)return get_menu(r,sp);
    if(op==0xa9c0)return get_new_mbar(r,sp);
    if(op==0xa933)return append(r,sp);
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
    if(op==0xa950){unsigned n; if(!mac_menu_record(r,rd(r,sp,4),&n))return fail(r,"CountMItems menu is invalid.");wr(r,sp+4,2,n);mac_cpu_set(r->cpu,MAC_A7,sp+4);return 1;}
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
        int before=(int16_t)rd(r,sp,2);uint32_t h=rd(r,sp+2,4);MacHandle*m=mac_menu_record(r,h,NULL);
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
