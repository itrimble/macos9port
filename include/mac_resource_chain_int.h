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
    char sidecar[40];      /* its file name in the overlay */
} MacOpenResource;
struct MacResourceChain {
    MacOpenResource files[MAC_FORK_LIMIT];
    int16_t current;
    uint32_t serial;
    uint16_t next_ref;
};
#endif
