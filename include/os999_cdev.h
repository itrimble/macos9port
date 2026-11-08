#ifndef OS999_CDEV_H
#define OS999_CDEV_H

#include <stddef.h>
#include <stdint.h>
#include "draw.h"
#include "ospi.h"
#include "control_panel_store.h"

enum {
    OS999_CDEV_API_VERSION = 1,
    OS999_CDEV_MAX_MODULES = 32
};

#define OS999_CDEV_ENTRY_SYMBOL "os999_cdev_module"

typedef enum {
    OS999_CDEV_CAT_ALL = 0,
    OS999_CDEV_CAT_APPEARANCE = 1,
    OS999_CDEV_CAT_SOUND = 2,
    OS999_CDEV_CAT_SYSTEM = 3,
    OS999_CDEV_CAT_HARDWARE = 4
} os999_cdev_category_t;

typedef enum {
    OS999_CDEV_EV_DOWN = 1,
    OS999_CDEV_EV_MOVE = 2,
    OS999_CDEV_EV_UP = 3,
    OS999_CDEV_EV_WHEEL = 4,
    OS999_CDEV_EV_KEY = 5
} os999_cdev_event_type_t;

typedef struct {
    int type;
    int x;
    int y;
    int clicks;
    int dy;
    int key;
    int mods;
    uint32_t now_ms;
} os999_cdev_event_t;

typedef struct os999_cdev os999_cdev_t;

typedef struct {
    Session *session;
    os999_cp_store_t *store;
    int (*get_int)(os999_cp_store_t *store, const char *panel_id, const char *key, int fallback);
    int (*set_int)(os999_cp_store_t *store, const char *panel_id, const char *key, int value);
    int (*get_bool)(os999_cp_store_t *store, const char *panel_id, const char *key, int fallback);
    int (*set_bool)(os999_cp_store_t *store, const char *panel_id, const char *key, int value);
    int (*get_str)(os999_cp_store_t *store, const char *panel_id, const char *key,
                   char *out, size_t out_n, const char *fallback);
    int (*set_str)(os999_cp_store_t *store, const char *panel_id, const char *key, const char *value);
    int (*flush_store)(os999_cp_store_t *store);
    void (*play_sound)(Session *s, const char *sound_name);
    void (*set_volume)(Session *s, int percent);
    void (*set_muted)(Session *s, int muted);
    int (*set_startup_chime)(Session *s, int enabled);
    int (*set_startup_intro)(Session *s, int enabled);
} os999_cdev_host_t;

struct os999_cdev {
    uint32_t api_version;
    const char *id;
    const char *title;
    const char *summary;
    const char *icon_name;
    int category;
    int (*init)(os999_cdev_t *self, const os999_cdev_host_t *host);
    void (*shutdown)(os999_cdev_t *self);
    int (*load_state)(os999_cdev_t *self, const os999_cdev_host_t *host);
    int (*save_state)(os999_cdev_t *self, const os999_cdev_host_t *host);
    int (*handle_event)(os999_cdev_t *self, const os999_cdev_host_t *host,
                        const os999_cdev_event_t *ev, Rect content);
    void (*render)(os999_cdev_t *self, const os999_cdev_host_t *host,
                   Fb *fb, Rect content);
    void *priv;
};

typedef const os999_cdev_t *(*os999_cdev_entry_fn)(void);

int os999_cdev_register(const os999_cdev_t *module);
int os999_cdev_load_plugin(const char *shared_lib_path);
int os999_cdev_scan_directory(const char *directory_path);
void os999_cdev_unregister_all(void);
int os999_cdev_count(void);
os999_cdev_t *os999_cdev_at(int index);
os999_cdev_t *os999_cdev_find(const char *id);
const char *os999_cdev_category_label(int category);

const os999_cdev_t *os999_cdev_general_module(void);
const os999_cdev_t *os999_cdev_sound_module(void);
const os999_cdev_t *os999_cdev_appearance_module(void);
const os999_cdev_t *os999_cdev_monitors_module(void);
const os999_cdev_t *os999_cdev_keyboard_module(void);
const os999_cdev_t *os999_cdev_energy_module(void);
const os999_cdev_t *os999_cdev_memory_module(void);
const os999_cdev_t *os999_cdev_network_module(void);

#endif
