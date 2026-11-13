#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "mac_resource_chain_int.h"
#include "mac_files_int.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Read-only exported resource forks on the pinned application volume. Names
 * map case-insensitively to sibling <name>.rsrc exports (raw or AppleDouble).
 * Ambiguous names fail rather than selecting filesystem enumeration order.
 * Public OpenResFile/UseResFile/CloseResFile interfaces: More Macintosh Toolbox
 * Resource Manager reference, pp.1-62..1-69. No ROM/source implementation used. */
enum { FORK_BYTE_LIMIT=16*1024*1024, DIRECTORY_LIMIT=4096, RESOURCE_LIMIT=4096 };
static int valid(MacCpu *c,uint32_t p,size_t n) {
    return p<=mac_cpu_ram_size(c) && n<=mac_cpu_ram_size(c)-p;
}
static int equal(const char *a,const char *b) { return mr_host_equal_fold(a,b); }   /* Mac Roman case folding, either Unicode normalisation (macroman.h) */
/* name is a host-relative path ("dir/leaf"); the export is "<leaf>.rsrc" in the same directory, matched without regard to case. On success path is the
 * export's relative path. */
static int resolve(int root,const char *name,char path[256]) {
    char dirpart[256];const char *leaf;
    mac_dirs_parent(name,dirpart,&leaf);
    char export_name[261];size_t length=strlen(leaf);
    if(length>250||strlen(name)>240)return -37;
    memcpy(export_name,leaf,length);memcpy(export_name+length,".rsrc",6);
    DIR *dir=mac_dirs_opendir(root,dirpart);
    if(!dir)return errno==ENOENT||errno==ENOTDIR?-43:-36;
    unsigned scanned=0,matches=0;int error=0,base_exists=0;
    for(;;) {
        errno=0;struct dirent *e=readdir(dir);
        if(!e){if(errno)error=-36;break;}
        if(++scanned>DIRECTORY_LIMIT){error=-108;break;}
        if(equal(e->d_name,leaf))base_exists=1;
        if(!equal(e->d_name,export_name))continue;
        char found[512];
        if(snprintf(found,sizeof found,"%s%s%s",dirpart,dirpart[0]?"/":"",e->d_name)>=256){error=-37;break;}
        struct stat st;
        if(mac_dirs_stat(root,found,&st)){error=-36;break;}
        if(!S_ISREG(st.st_mode)){error=-54;break;}
        if(++matches>1){error=-37;break;}
        memcpy(path,found,strlen(found)+1);
    }
    if(closedir(dir)&&!error)error=-36;
    return error?error:matches?0:base_exists?-1:-43;
}
static int read_fork(MacRuntime *rt,const char *name,MacOpenResource *out) {
    int root=mac_files_directory_fd(rt);if(root<0)return -35;
    char path[256];int error=0,fd=-1;
    /* A resource fork on the save overlay ("<name>.rsrc", see mac_files_rf.c) shadows the application volume's export and is writable. */
    FileState *fs=rt->file_state;out->writable=0;out->sidecar[0]=0;
    const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
    if(mr_sidecar_ok(leaf) && strlen(name)+6<=sizeof path) {
        snprintf(out->sidecar,sizeof out->sidecar,"%s.rsrc",name); /* where a write goes: the overlay copy, made on the first write */
        if(fs && fs->saves>=0) {
            mac_files_flush_staged(fs);
            snprintf(path,sizeof path,"%s.rsrc",name);
            fd=mac_dirs_openat(fs->saves,path,O_RDONLY|O_NONBLOCK,0);
            if(fd>=0)out->writable=1;
        }
    }
    if(fd<0) {
        error=resolve(root,name,path);
        if(error && rt->trace)fprintf(stderr,"resource-open name=%s error=%d\n",name,error);
        if(error==-1){mac_cpu_halt(rt->cpu,"Native resource fork opening is unsupported; an exported .rsrc fork is required.");return -1;}
        if(error)return error;
        fd=mac_dirs_openat(root,path,O_RDONLY|O_NONBLOCK,0);
        if(fd<0)return errno==ELOOP||errno==EACCES?-54:errno==ENOENT?-43:-36;
    }
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);return -36;}
    if(st.st_size<16||st.st_size>FORK_BYTE_LIMIT){close(fd);return -199;}
    size_t size=(size_t)st.st_size;uint8_t *bytes=malloc(size);
    if(!bytes){close(fd);return -108;}
    size_t offset=0;
    while(offset<size) {
        ssize_t n=read(fd,bytes+offset,size-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){error=-36;break;}
        offset+=(size_t)n;
    }
    if(close(fd)&&!error)error=-36;
    if(!error && (mac_resource_open_container(&out->fork,bytes,size,NULL,0)||
                  mac_resource_count(&out->fork)>RESOURCE_LIMIT))error=-199;
    if(error){free(bytes);return error;}
    out->bytes=bytes;out->device=st.st_dev;out->inode=st.st_ino;
    if(rt->trace)fprintf(stderr,"resource-open name=%s file=%s bytes=%zu resources=%zu\n",name,path,size,mac_resource_count(&out->fork));
    return 0;
}
static struct MacResourceChain *ensure(MacRuntime *rt) {
    if(!rt->resource_chain) {
        rt->resource_chain=calloc(1,sizeof *rt->resource_chain);
        if(rt->resource_chain){rt->resource_chain->current=MAC_APP_RES_REF;rt->resource_chain->next_ref=MAC_FORK_REF_BASE;}
    }
    return rt->resource_chain;
}
struct MacResourceChain *mac_resource_chain_ensure(MacRuntime *rt) { return ensure(rt); }
MacOpenResource *mac_resource_file_of(MacRuntime *rt,int16_t ref) {
    struct MacResourceChain *c=rt->resource_chain;
    if(c&&ref>=MAC_FORK_REF_BASE)for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes&&c->files[i].ref==ref)return &c->files[i];
    return NULL;
}
/* OpenResFile(fileName): the name is a Pascal string, resolved against the default directory (a partial path or "OS999 HD:dir:name" also works). */
int mac_resource_open_by_pointer(MacRuntime *rt,uint32_t pointer,int16_t *ref) {
    if(!pointer||!valid(rt->cpu,pointer,1))return -37;
    char name[DIRS_PATH_MAX];
    int error=mac_files_ptr_path(rt,pointer,0,0,name);
    if(error==-50||(!error&&!name[0]))return -37;
    if(error)return error;
    return mac_resource_open_path(rt,name,ref);
}
/* OpenResFile of a file by host-relative path (also what FSpOpenResFile uses). */
int mac_resource_open_path(MacRuntime *rt,const char *name,int16_t *ref) {
    struct MacResourceChain *c=ensure(rt);if(!c)return -108;
    MacOpenResource opened={0};int error=read_fork(rt,name,&opened);if(error)return error;
    for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes &&
       c->files[i].device==opened.device && c->files[i].inode==opened.inode) {
        *ref=c->files[i].ref;free(opened.bytes);return 0; /* Reopen does not change current. */
    }
    size_t resources=mac_resource_count(&rt->fork)+mac_resource_count(&opened.fork);
    for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes)resources+=mac_resource_count(&c->files[i].fork);
    if(resources>INT16_MAX){free(opened.bytes);return -108;}
    unsigned slot=0;while(slot<MAC_FORK_LIMIT&&c->files[slot].bytes)slot++;
    if(slot==MAC_FORK_LIMIT||c->next_ref>INT16_MAX||c->serial==UINT32_MAX){free(opened.bytes);return -42;}
    opened.ref=(int16_t)c->next_ref;opened.serial=c->serial+1;
    c->files[slot]=opened;
    uint32_t heap=rt->heap_next;size_t handles=rt->handle_count;
    for(size_t i=0;i<mac_resource_count(&opened.fork);i++) {
        MacResource r;mac_resource_at(&opened.fork,i,&r);
        if((r.attributes&4)&&!mac_runtime_resource_owned(rt,r.type,r.id,opened.ref)){error=-108;break;}
    }
    if(error) {
        for(size_t i=0;i<rt->handle_count;i++)if(rt->handles[i].resource_owner==opened.ref) {
            mac_runtime_free_data(rt,rt->handles[i].pointer);
            memset(&rt->handles[i],0,sizeof rt->handles[i]);
        }
        rt->heap_next=heap;rt->handle_count=handles;
        free(opened.bytes);memset(&c->files[slot],0,sizeof c->files[slot]);return error;
    }
    c->next_ref++;c->serial++;c->current=opened.ref;*ref=opened.ref;return 0;
}
static int close_resource(MacRuntime *rt,int16_t ref) {
    struct MacResourceChain *c=rt->resource_chain;
    if(!ref||ref==MAC_SYSTEM_RES_REF||ref==MAC_APP_RES_REF) {
        mac_cpu_halt(rt->cpu,"Closing boot resource forks is unsupported.");return -1;
    }
    MacOpenResource *file=NULL;
    if(c)for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes&&c->files[i].ref==ref)file=&c->files[i];
    if(!file)return -193;
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *h=&rt->handles[i];
        if(h->resource&&h->resource_owner==ref && (h->loaded_code||!valid(rt->cpu,h->handle,4)||(h->handle&1))) {
            mac_cpu_halt(rt->cpu,"CloseResFile cannot invalidate active or malformed resource handles.");return -1;
        }
    }
    int flushed=mac_resource_flush_file(rt,file);
    if(rt->trace)fprintf(stderr,"resource-close ref=%d flush=%d\n",ref,flushed);
    /* a failed write is reported in ResError; the file is closed all the same */
    for(size_t i=0;i<rt->handle_count;i++) {
        MacHandle *h=&rt->handles[i];
        if(h->resource&&h->resource_owner==ref){mac_cpu_write(rt->cpu,h->handle,4,0);mac_runtime_free_data(rt,h->pointer);memset(h,0,sizeof *h);}
    }
    free(file->bytes);memset(file,0,sizeof *file);
    if(c->current==ref) {
        c->current=MAC_APP_RES_REF;uint32_t newest=0;
        for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes&&c->files[i].serial>newest){newest=c->files[i].serial;c->current=c->files[i].ref;}
    }
    return flushed;
}
int mac_resource_file_service(MacRuntime *rt,uint16_t opcode) {
    unsigned args,width;
    switch(opcode) {
    case 0xa997:args=4;width=2;break;
    case 0xa9c4:args=8;width=2;break;                                   /* OpenRFPerm(fileName, vRefNum, permission): the resource file of the named file, as OpenResFile (permission and volume are not distinguished) */
    case 0xa994:args=0;width=2;break;
    case 0xa998:case 0xa99a:args=2;width=0;break;
    case 0xa9a4:args=4;width=2;break;
    default:return 0;
    }
    MacCpu *cpu=rt->cpu;uint32_t sp=mac_cpu_get(cpu,MAC_A7);
    if((sp&1)||!valid(cpu,sp,args+width)){mac_cpu_halt(cpu,"Invalid resource file stack.");return -1;}
    int error=0;int16_t value=-1;
    struct MacResourceChain *c=rt->resource_chain;
    if(opcode==0xa997)error=mac_resource_open_by_pointer(rt,mac_cpu_read(cpu,sp,4),&value);
    else if(opcode==0xa9c4)error=mac_resource_open_by_pointer(rt,mac_cpu_read(cpu,sp+4,4),&value);
    else if(opcode==0xa994)value=c?c->current:MAC_APP_RES_REF;
    else if(opcode==0xa9a4) {
        uint32_t h=mac_cpu_read(cpu,sp,4);error=-192;
        for(size_t i=0;i<rt->handle_count;i++)if(h&&rt->handles[i].handle==h&&rt->handles[i].resource) {
            value=rt->handles[i].resource_owner?rt->handles[i].resource_owner:MAC_APP_RES_REF;error=0;break;
        }
    } else {
        int16_t ref=(int16_t)mac_cpu_read(cpu,sp,2);
        if(opcode==0xa99a)error=close_resource(rt,ref);
        else {
            if(!ref)ref=MAC_SYSTEM_RES_REF;
            c=ensure(rt);if(!c)error=-108;
            else if(ref==MAC_APP_RES_REF||ref==MAC_SYSTEM_RES_REF||mac_resource_owner_fork(rt,ref))c->current=ref;
            else error=-193;
        }
    }
    if(mac_cpu_halted(cpu))return -1;
    if(width)mac_cpu_write(cpu,sp+args,width,(uint16_t)value);
    mac_cpu_write(cpu,0xa60,2,(uint16_t)error);mac_cpu_set(cpu,MAC_A7,sp+args);return 1;
}
