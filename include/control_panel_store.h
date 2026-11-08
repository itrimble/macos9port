#ifndef OS999_CONTROL_PANEL_STORE_H
#define OS999_CONTROL_PANEL_STORE_H

#include <pthread.h>
#include <stddef.h>
#include "ospi.h"

enum {
    OS999_CP_MAX_ENTRIES = 128,
    OS999_CP_ID_MAX = 32,
    OS999_CP_KEY_MAX = 48,
    OS999_CP_STR_MAX = 128
};

typedef enum {
    OS999_CP_VAL_INT = 1,
    OS999_CP_VAL_BOOL = 2,
    OS999_CP_VAL_STR = 3
} os999_cp_val_kind_t;

typedef struct {
    char panel_id[OS999_CP_ID_MAX];
    char key[OS999_CP_KEY_MAX];
    int kind;
    int int_val;
    char str_val[OS999_CP_STR_MAX];
} os999_cp_entry_t;

typedef struct os999_cp_store {
    pthread_mutex_t lock;
    int initialized;
    char path[OSPI_PATH + 48];
    os999_cp_entry_t entries[OS999_CP_MAX_ENTRIES];
    int count;
} os999_cp_store_t;

#define OS999_CP_STORE_FILENAME ".os999-control-panels.json"

int os999_cp_store_init(os999_cp_store_t *store, const char *volume_root);
void os999_cp_store_destroy(os999_cp_store_t *store);

/* Reads existing JSON file if present; missing file succeeds with 0 entries. */
int os999_cp_store_load(os999_cp_store_t *store);
/* Atomically serializes all panel settings to JSON via temp-file + rename. */
int os999_cp_store_save(os999_cp_store_t *store);

int os999_cp_store_has(os999_cp_store_t *store, const char *panel_id, const char *key);
int os999_cp_store_get_int(os999_cp_store_t *store, const char *panel_id,
                           const char *key, int fallback);
int os999_cp_store_set_int(os999_cp_store_t *store, const char *panel_id,
                           const char *key, int value);
int os999_cp_store_get_bool(os999_cp_store_t *store, const char *panel_id,
                            const char *key, int fallback);
int os999_cp_store_set_bool(os999_cp_store_t *store, const char *panel_id,
                            const char *key, int value);
int os999_cp_store_get_str(os999_cp_store_t *store, const char *panel_id,
                           const char *key, char *out, size_t out_n,
                           const char *fallback);
int os999_cp_store_set_str(os999_cp_store_t *store, const char *panel_id,
                           const char *key, const char *value);

#endif
