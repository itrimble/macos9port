#ifndef OS999_EXT_REGISTRY_H
#define OS999_EXT_REGISTRY_H

/* OS999 Extensions: discovery, load order, persisted enable state and the
 * boot-time override decision. Host side only; it never executes guest code.
 *
 * Layout on the volume:
 *   System Folder/Extensions/<Name>          extension (resource fork container)
 *   System Folder/Extensions/<Name>.rsrc     optional resource-fork sidecar
 *   System Folder/Extensions/<Name>.extinfo  optional OS999 text sidecar:
 *        priority <int>   lower loads first (default 100)
 *        requires <Name>  repeatable, at most EXT_DEPS
 * State lives in <volume>/.os999-extensions (header OS999-EXT-1). */

#include <stddef.h>

enum { EXT_MAX = 64, EXT_NAME = 64, EXT_PATH = 640, EXT_DEPS = 4, EXT_SETS = 8,
       EXT_PRIORITY_DEFAULT = 100 };

typedef enum {
    EXT_PENDING = 0, EXT_LOADED, EXT_DISABLED, EXT_FAILED, EXT_MISSING_DEP, EXT_CYCLE
} ExtStatus;

typedef struct {
    char name[EXT_NAME];
    char path[EXT_PATH];
    char fork_path[EXT_PATH];
    int enabled, priority, dep_count;
    char deps[EXT_DEPS][EXT_NAME];
    ExtStatus status;
    char error[96];
} ExtEntry;

typedef struct {
    char name[EXT_NAME];
    char enabled[EXT_MAX][EXT_NAME];
    int count;
} ExtSet;

typedef struct {
    ExtEntry items[EXT_MAX];
    int count;
    int load[EXT_MAX], load_count; /* indexes into items, in load order */
    char disabled[EXT_MAX][EXT_NAME]; /* persisted, may name absent extensions */
    int disabled_count;
    ExtSet sets[EXT_SETS];
    int set_count;
    int crash_pending; /* previous boot began loading and never finished */
} ExtList;

typedef enum { EXT_BOOT_NORMAL = 0, EXT_BOOT_MANAGER, EXT_BOOT_NONE } ExtBootMode;

/* Scan <root>/System Folder/Extensions. Returns the entry count or -1. */
int ext_scan(ExtList *list, const char *root);
/* Compute list->load and per-entry status. Never fails; diagnostics go in status/error. */
void ext_order(ExtList *list);
ExtEntry *ext_find(ExtList *list, const char *name);
/* Enable/disable by name (also for absent extensions). 0 ok, -1 invalid name. */
int ext_set_enabled(ExtList *list, const char *name, int enabled);
int ext_valid_name(const char *name);

int ext_state_load(ExtList *list, const char *root);   /* apply D lines, sets, crash flag */
int ext_state_save(const ExtList *list, const char *root);
int ext_state_begin_boot(const ExtList *list, const char *root); /* marks boot_pending */
int ext_state_end_boot(const ExtList *list, const char *root);   /* clears it */
int ext_set_save(ExtList *list, const char *set_name);  /* snapshot current enabled names */
int ext_set_apply(ExtList *list, const char *set_name); /* 0 ok, -1 unknown set */

/* Space held (or OSPI_EXT_MANAGER=1, or a crash-looped previous boot) opens the
 * manager; Shift alone loads nothing. Pure so tests need no window system. */
ExtBootMode ext_boot_decide(int space_held, int shift_held, int env_manager, int crash_pending);
/* Mode applied to the list: NONE disables loading for this boot without
 * rewriting the persisted state (statuses become EXT_DISABLED). */
void ext_boot_apply(ExtList *list, ExtBootMode mode);

const char *ext_status_label(ExtStatus status);

#endif
