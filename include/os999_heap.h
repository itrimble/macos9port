#ifndef OS999_HEAP_H
#define OS999_HEAP_H

/* OS999 protected memory manager.
 *
 * Each guest gets an OsSpace: a private, host-reserved virtual region that is
 * committed on demand (auto-growing heap, no Preferred/Minimum Size). Guest
 * addresses are translated and bounds-checked; a bad access faults and tears
 * down only that space. Block metadata lives outside guest memory so a
 * misbehaving guest cannot corrupt the allocator. Handles carry a generation
 * so stale handles are rejected instead of aliasing a new block.
 *
 * Not yet wired into mac_runtime_alloc(); see docs for the integration seam. */

#include <stddef.h>
#include <stdint.h>

#define OS999_GUEST_BASE UINT32_C(0x1000) /* guest address of arena offset 0 */

/* Classic Memory Manager result codes where one exists. */
enum {
    OS999_OK = 0,
    OS999_ERR_PARAM = -50,
    OS999_ERR_MEMFULL = -108,
    OS999_ERR_NILHANDLE = -109,
    OS999_ERR_LOCKED = -117,
    OS999_ERR_DEAD = -9001 /* space faulted or terminated */
};

typedef enum { OS999_FAULT_NONE, OS999_FAULT_UNMAPPED, OS999_FAULT_OUT_OF_BLOCK } Os999FaultKind;
typedef enum { OS999_SPACE_ALIVE, OS999_SPACE_FAULTED, OS999_SPACE_TERMINATED } Os999SpaceState;

typedef struct {
    size_t initial_commit; /* bytes committed at creation (rounded to chunk) */
    size_t grow_chunk;     /* commit granularity, rounded to host page size */
    size_t max_bytes;      /* hard ceiling for this space (the "partition") */
    size_t max_handles;    /* cap on live blocks; 0 = default */
} Os999Config;

typedef struct {
    Os999FaultKind kind;
    uint32_t address, length;
    int write;
} Os999Fault;

typedef struct Os999Space Os999Space;
typedef struct Os999Heap Os999Heap;

Os999Config os999_default_config(void);
Os999Space *os999_space_create(const Os999Config *cfg); /* NULL on failure */
void os999_space_destroy(Os999Space *space);            /* idempotent after fault */
Os999SpaceState os999_space_state(const Os999Space *space);
const Os999Fault *os999_space_fault(const Os999Space *space);
/* NULL once the space has faulted or been terminated. */
Os999Heap *os999_space_heap(Os999Space *space);
void os999_space_terminate(Os999Space *space); /* release memory, keep fault record */

/* Checked translation to a host pointer. Any violation records a fault, tears
 * the space down, and returns NULL. translate() checks the committed arena;
 * translate_block() also requires [addr, addr+len) inside one live block. */
uint8_t *os999_translate(Os999Space *space, uint32_t addr, uint32_t len, int write);
uint8_t *os999_translate_block(Os999Space *space, uint32_t addr, uint32_t len, int write);

/* Relocatable blocks. Handles are opaque nonzero tokens. */
int os999_new_handle(Os999Heap *h, uint32_t size, uint32_t *handle);
int os999_dispose_handle(Os999Heap *h, uint32_t handle);
int os999_deref(const Os999Heap *h, uint32_t handle, uint32_t *address);
int os999_handle_size(const Os999Heap *h, uint32_t handle, uint32_t *size);
int os999_set_handle_size(Os999Heap *h, uint32_t handle, uint32_t size);
int os999_lock(Os999Heap *h, uint32_t handle);
int os999_unlock(Os999Heap *h, uint32_t handle);

/* Non-relocatable blocks, addressed directly. */
int os999_new_ptr(Os999Heap *h, uint32_t size, uint32_t *address);
int os999_dispose_ptr(Os999Heap *h, uint32_t address);

/* Defragmentation: slide unlocked blocks down, at most ~budget bytes moved per
 * call (at least one block when work exists). Returns bytes moved; 0 = done. */
size_t os999_compact_step(Os999Heap *h, size_t budget);

typedef struct {
    size_t committed, max_bytes, used, free_total, largest_free, live_blocks;
    size_t compactions, grows;
} Os999Stats;
void os999_heap_stats(const Os999Heap *h, Os999Stats *out);
/* Free space must be all zero (no stale data). Returns 1 when the invariant holds. */
int os999_heap_check(const Os999Heap *h);

/* Process-wide accounting for leak checks. */
size_t os999_live_spaces(void);
size_t os999_reserved_bytes(void);

#endif
