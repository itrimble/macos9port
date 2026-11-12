#ifndef OS999_VOLUME_CAPACITY_H
#define OS999_VOLUME_CAPACITY_H
/* Defined capacity for the internal disk (the OS999 volume directory).
 *
 * The capacity is a per-volume number stored in <volume>/.os999/capacity. Used
 * bytes are the regular files under the volume (hidden and .os999 included)
 * plus the guest save folders that sit beside it in <volume>/../Saves.
 * Free = min(capacity - used, free space the host can still supply). Writes
 * OS999 itself makes are refused with dskFulErr (-34) when they would pass it.
 * Processes outside OS999 can still write into the folder; they are counted
 * the next time the volume is measured but cannot be stopped. */
#include <stddef.h>
#include <stdint.h>

#define VOLUME_DISK_NAME "OS999 HD"      /* what the desktop icon says, and what guests see */
enum { VOLUME_DISK_FULL = -34 };         /* classic dskFulErr */

#define VOLUME_CAPACITY_DEFAULT_BYTES  (UINT64_C(2) << 30)    /* 2 GiB */
#define VOLUME_CAPACITY_HEADROOM_BYTES (UINT64_C(512) << 20)  /* free room kept above existing usage */
#define VOLUME_CAPACITY_MIN_BYTES      (UINT64_C(64) << 20)
#define VOLUME_CAPACITY_MAX_BYTES      (UINT64_C(1) << 40)    /* 1 TiB */
#define VOLUME_CAPACITY_STEP_BYTES     (UINT64_C(1) << 30)    /* control panel "+1 GB" */

typedef struct {
    uint64_t capacity;   /* defined size of the disk */
    uint64_t used;       /* bytes in use */
    uint64_t host_free;  /* what the host file system can still supply */
    uint64_t free;       /* min(capacity - used, host_free) */
} VolumeSpace;

/* Pure rules (no file access). */
void volume_space_compute(uint64_t capacity, uint64_t used, uint64_t host_free, VolumeSpace *out);
uint64_t volume_capacity_default_for(uint64_t used, uint64_t host_free);
/* 0 if `requested` is an acceptable capacity, else -1 with a sentence in msg. */
int volume_capacity_check(uint64_t requested, uint64_t used, uint64_t host_free, char *msg, size_t n);

/* Measurement and persistence. */
int volume_used_bytes(const char *root, uint64_t *out);
int volume_host_free(const char *root, uint64_t *out);
int volume_capacity_load(const char *root, uint64_t *out, int *stored);  /* stored value, else the default; never writes */
int volume_capacity_ensure(const char *root);                            /* write the default if nothing is stored */
int volume_capacity_set(const char *root, uint64_t bytes, char *msg, size_t n);  /* validate, then store */
int volume_space_at(const char *root, uint64_t host_free, VolumeSpace *out);     /* fresh walk, explicit host free (tests) */
int volume_space(const char *root, int fresh, VolumeSpace *out);                 /* host free from statvfs; !fresh may use a 5 s cache */
void volume_space_invalidate(void);
/* 0 if `bytes` more would fit, else -34 and (optionally) a sentence in msg. Fresh walk. */
int volume_space_reserve(const char *root, uint64_t bytes, char *msg, size_t n);
/* The sentence from the most recent refusal by volume_space_reserve, or NULL. clear!=0 forgets it. Lets a
 * Finder action that failed say "the disk is full" instead of a generic failure. */
const char *volume_disk_full_notice(int clear);
void volume_format_bytes(char *out, size_t n, uint64_t bytes);

/* Cheap per-process budget for a stream of small writes (guest runtime): one
 * walk per second, then the growth already approved is subtracted. */
typedef struct { char root[1024]; uint64_t free, spent; int64_t stamp_ms; int active; } VolumeBudget;
int volume_budget_open(VolumeBudget *b, const char *root);
int volume_budget_spend(VolumeBudget *b, uint64_t bytes);       /* 0, or -34 */
int volume_budget_space(VolumeBudget *b, VolumeSpace *out);     /* fresh figures for PBHGetVInfo */

/* Classic 16-bit allocation-block view of a size in bytes (PBHGetVInfo). */
typedef struct { uint16_t total_blocks, free_blocks; uint32_t block_size, total_bytes; } VolumeClassic;
void volume_classic_view(const VolumeSpace *space, VolumeClassic *out);
#endif
