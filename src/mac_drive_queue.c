#include "mac_drive_queue.h"
#include <stdlib.h>
#include <string.h>

/* Public DrvQEl and preceding flags, not an implementation copied from an OS:
 * https://leopard-adc.pepas.com/documentation/mac/Files/Files-112.html
 * DrvQHdr at $308: flags word, head long, tail long. Logical virtual volumes
 * have readonly nonremovable backing; dQRefNum=0 explicitly denotes no driver.
 */
enum { HEADER=0x308, HEADER_BYTES=10, ENTRY_BYTES=20, DRIVE_COUNT=2 };
struct MacDriveQueue { uint32_t storage, blocks[DRIVE_COUNT]; unsigned mounted; };
static int range(MacCpu *cpu, uint32_t p, size_t n) {
    size_t size=mac_cpu_ram_size(cpu);
    return p<=size && n<=size-p;
}
static void w16(uint8_t *p,uint16_t v) { p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v; }
static void w32(uint8_t *p,uint32_t v) { w16(p,(uint16_t)(v>>16));w16(p+2,(uint16_t)v); }
static int rebuild(MacRuntime *rt) {
    struct MacDriveQueue *q=rt->drive_queue;
    if(!rt->cpu || !q || !range(rt->cpu,HEADER,HEADER_BYTES) ||
       !range(rt->cpu,q->storage,ENTRY_BYTES*DRIVE_COUNT))return -1;
    uint8_t entries[ENTRY_BYTES*DRIVE_COUNT]={0},header[HEADER_BYTES]={0};
    uint32_t head=0,tail=0;
    for(unsigned i=0;i<DRIVE_COUNT;i++) {
        if(!(q->mounted&(1u<<i)))continue;
        uint32_t entry=q->storage+ENTRY_BYTES*i+4;
        uint8_t *flags=entries+ENTRY_BYTES*i,*e=flags+4;
        flags[0]=0x80; /* Locked. */
        flags[1]=8; /* Present and nonejectable; no driver callback. */
        w16(e+4,1);w16(e+6,(uint16_t)(i+1));
        /* Driver ref and filesystem ID remain zero: File Manager-owned. */
        w16(e+12,(uint16_t)q->blocks[i]);w16(e+14,(uint16_t)(q->blocks[i]>>16));
        if(tail)w32(entries+(tail-q->storage),entry);else head=entry;
        tail=entry;
    }
    w32(header+2,head);w32(header+6,tail);
    /* Raw checked RAM is intentional: teardown also runs after guest faults. */
    memcpy(mac_cpu_ram(rt->cpu)+q->storage,entries,sizeof entries);
    memcpy(mac_cpu_ram(rt->cpu)+HEADER,header,sizeof header);
    return 0;
}
int mac_drive_mount(MacRuntime *rt,unsigned drive,uint32_t blocks) {
    if(!rt || !rt->cpu || mac_cpu_halted(rt->cpu) || drive<1 || drive>DRIVE_COUNT ||
       !range(rt->cpu,HEADER,HEADER_BYTES))return -1;
    if(!rt->drive_queue) {
        if(rt->heap_next<HEADER+HEADER_BYTES || (rt->heap_next&1) ||
           !range(rt->cpu,rt->heap_next,ENTRY_BYTES*DRIVE_COUNT) ||
           rt->heap_next>rt->heap_limit || ENTRY_BYTES*DRIVE_COUNT>rt->heap_limit-rt->heap_next)return -1;
        struct MacDriveQueue *q=calloc(1,sizeof *q);
        if(!q)return -1;
        uint32_t old_heap=rt->heap_next;
        q->storage=mac_runtime_alloc(rt,ENTRY_BYTES*DRIVE_COUNT);
        if(!q->storage || (q->storage&1) || q->storage<HEADER+HEADER_BYTES ||
           !range(rt->cpu,q->storage,ENTRY_BYTES*DRIVE_COUNT)) {
            rt->heap_next=old_heap;free(q);return -1;
        }
        rt->drive_queue=q;
    }
    rt->drive_queue->blocks[drive-1]=blocks;
    rt->drive_queue->mounted|=1u<<(drive-1);
    return rebuild(rt);
}
unsigned mac_drive_number(const MacRuntime *rt,unsigned drive) {
    return rt && rt->drive_queue && drive>=1 && drive<=DRIVE_COUNT &&
           (rt->drive_queue->mounted&(1u<<(drive-1))) ? drive : 0;
}
void mac_drive_unmount(MacRuntime *rt,unsigned drive) {
    if(!rt || !rt->drive_queue || drive<1 || drive>DRIVE_COUNT)return;
    rt->drive_queue->mounted&=~(1u<<(drive-1));
    (void)rebuild(rt);
}
void mac_drive_destroy(MacRuntime *rt) {
    if(!rt || !rt->drive_queue)return;
    rt->drive_queue->mounted=0;(void)rebuild(rt);
    free(rt->drive_queue);rt->drive_queue=NULL;
}
