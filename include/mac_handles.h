#ifndef MAC_HANDLES_H
#define MAC_HANDLES_H
#include "mac_runtime.h"
/* Guest blocks for a service's own results, made by the same Memory Manager calls the guest uses (so the guest may lock, size, read and dispose them like any
 * other block). The caller's D0, A0, status register and MemErr are preserved. new: a zeroed relocatable block, 0 when memory is short. */
uint32_t mac_handle_new(MacRuntime *rt, uint32_t size);
void mac_handle_dispose(MacRuntime *rt, uint32_t handle);
uint32_t mac_ptr_new(MacRuntime *rt, uint32_t size);          /* a zeroed nonrelocatable block (NewPtrClear), 0 when memory is short */
/* The block behind a handle the Memory Manager knows: 1 with *pointer and *size, 0 for anything else (nil, forged, purged, master pointer changed). */
int mac_handle_block(const MacRuntime *rt, uint32_t handle, uint32_t *pointer, uint32_t *size);
#endif
