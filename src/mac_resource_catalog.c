#include "mac_resource_chain_int.h"
#include <stdlib.h>

/* Public search order: current file, then older open files, then application
 * and authored empty System map. UseResFile changes the search start only.
 * https://dev.os9.ca/techpubs/mac/MoreToolbox/MoreToolbox-47.html */
const MacResourceFork *mac_resource_owner_fork(MacRuntime *rt, int16_t owner) {
    if (!owner) return &rt->fork;
    struct MacResourceChain *chain = rt->resource_chain;
    if (chain) for (unsigned i=0;i<MAC_FORK_LIMIT;i++)
        if (chain->files[i].bytes && chain->files[i].ref==owner) return &chain->files[i].fork;
    return NULL;
}
static unsigned order(MacRuntime *rt, const MacResourceFork **forks, int16_t *owners, int mode) {
    int one = mode == 1;
    struct MacResourceChain *c=rt->resource_chain;
    int16_t current=c?c->current:MAC_APP_RES_REF;
    if(mode==2 && c) {
        uint32_t newest=0;
        current=MAC_APP_RES_REF;
        for(unsigned i=0;i<MAC_FORK_LIMIT;i++)if(c->files[i].bytes && c->files[i].serial>newest) {newest=c->files[i].serial;current=c->files[i].ref;}
    }
    if(current==MAC_SYSTEM_RES_REF)return 0; /* Authored System contains no resources. */
    unsigned n=0; uint32_t ceiling=0;
    if(c && current!=MAC_APP_RES_REF) {
        for(unsigned i=0;i<MAC_FORK_LIMIT;i++)
            if(c->files[i].bytes && c->files[i].ref==current)ceiling=c->files[i].serial;
        while(ceiling) {
            MacOpenResource *best=NULL;
            for(unsigned i=0;i<MAC_FORK_LIMIT;i++) {
                MacOpenResource *f=&c->files[i];
                if(f->bytes && f->serial<=ceiling && (!best||f->serial>best->serial))best=f;
            }
            if(!best)break;
            forks[n]=&best->fork;owners[n++]=best->ref;
            if(one)return n;
            ceiling=best->serial-1;
        }
    }
    forks[n]=&rt->fork;owners[n++]=0;return n;
}
int mac_resource_chain_type_exists(MacRuntime *rt,uint32_t type,int one) {
    const MacResourceFork *forks[MAC_FORK_LIMIT+1];int16_t owners[MAC_FORK_LIMIT+1];
    unsigned n=order(rt,forks,owners,one);
    for(unsigned i=0;i<n;i++)for(size_t j=0;j<mac_resource_count(forks[i]);j++) {
        MacResource r;if(!mac_resource_at(forks[i],j,&r)&&r.type==type)return 1;
    }
    return 0;
}
int mac_resource_chain_find(MacRuntime *rt,uint32_t type,int16_t id,int one,
                            MacResource *out,int16_t *owner) {
    const MacResourceFork *forks[MAC_FORK_LIMIT+1];int16_t owners[MAC_FORK_LIMIT+1];
    unsigned n=order(rt,forks,owners,one);
    for(unsigned i=0;i<n;i++)if(!mac_resource_find(forks[i],type,id,out)) {
        *owner=owners[i];return 0;
    }
    return 1;
}
int mac_resource_chain_at(MacRuntime *rt,uint32_t type,unsigned index,int one,
                          MacResource *out,int16_t *owner) {
    if(!index)return 1;
    const MacResourceFork *forks[MAC_FORK_LIMIT+1];int16_t owners[MAC_FORK_LIMIT+1];
    unsigned n=order(rt,forks,owners,one ? 1 : 2),count=0;
    for(unsigned i=0;i<n;i++) for(size_t j=0;j<mac_resource_count(forks[i]);j++) {
        MacResource r;if(mac_resource_at(forks[i],j,&r)||r.type!=type)continue;
        if(++count==index){*out=r;*owner=owners[i];return 0;}
    }
    return 1;
}
unsigned mac_resource_chain_count(MacRuntime *rt,uint32_t type,int one) {
    const MacResourceFork *forks[MAC_FORK_LIMIT+1];int16_t owners[MAC_FORK_LIMIT+1];
    unsigned n=order(rt,forks,owners,one ? 1 : 2),count=0;
    for(unsigned i=0;i<n;i++)for(size_t j=0;j<mac_resource_count(forks[i]);j++) {
        MacResource r;if(!mac_resource_at(forks[i],j,&r)&&r.type==type)count++;
    }
    return count;
}
void mac_resource_chain_destroy(MacRuntime *rt) {
    if(!rt || !rt->resource_chain)return;
    for(unsigned i=0;i<MAC_FORK_LIMIT;i++)free(rt->resource_chain->files[i].bytes);
    free(rt->resource_chain);rt->resource_chain=NULL;
}
