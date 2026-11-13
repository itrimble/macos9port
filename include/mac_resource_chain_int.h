#ifndef MAC_RESOURCE_CHAIN_INT_H
#define MAC_RESOURCE_CHAIN_INT_H
#include "mac_resource_chain.h"
#include <sys/types.h>
enum { MAC_FORK_LIMIT = 8, MAC_FORK_REF_BASE = 256 };
typedef struct {
    MacResourceFork fork;
    uint8_t *bytes;
    dev_t device;
    ino_t inode;
    int16_t ref;
    uint32_t serial;
    int writable;          /* opened from the save overlay: UpdateResFile / CloseResFile write it back */
    int dirty;             /* the bytes above differ from the file: written by UpdateResFile, WriteResource or CloseResFile */
    char sidecar[300];     /* its file name in the overlay ("dir/leaf.rsrc"); empty when no sidecar could carry it */
} MacOpenResource;
struct MacResourceChain {
    MacOpenResource files[MAC_FORK_LIMIT];
    int16_t current;
    uint32_t serial;
    uint16_t next_ref;
    uint8_t purge_install; /* SetResPurge(true): recorded; nothing here is purged */
};
struct MacResourceChain *mac_resource_chain_ensure(MacRuntime *rt);
MacOpenResource *mac_resource_file_of(MacRuntime *rt, int16_t ref);   /* an open file other than the application's, or NULL */
/* The writable Resource Manager (mac_resource_write.c): the trap family, and the flush CloseResFile does. */
int mac_resource_write_service(MacRuntime *rt, uint16_t opcode);      /* 0 when the trap is not one of ours */
int mac_resource_flush_file(MacRuntime *rt, MacOpenResource *f);      /* pull changed handles into the fork and write it; 0 or a File Manager error */
#endif
