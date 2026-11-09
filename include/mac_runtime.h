#ifndef MAC_RUNTIME_H
#define MAC_RUNTIME_H

#include "mac_cpu.h"
#include "mac_resource.h"
#include "runtime_bridge.h"

#define MAC_CODE_TYPE UINT32_C(0x434f4445)
#define MAC_RUNTIME_RAM (16u * 1024u * 1024u)
#define MAC_RUNTIME_HANDLES 512
/* Smallest poll budget the VBL/Time Manager/sound pollers accept. */
#define MAC_RUNTIME_SERVICE_MIN 10000u
#define MAC_GESTALT_MAX 32

typedef struct {
    uint16_t what, modifiers;
    uint32_t message, when;
    int16_t x, y;
} MacEvent;

typedef struct {
    uint32_t handle, pointer, size, type;
    int16_t id, resource_owner; /* Zero identifies the application fork. */
    int resource;
    uint8_t state;
    uint8_t loaded_code; /* Execution addresses are published; never relocate/dispose. */
    uint8_t resource_changed;
} MacHandle;

typedef struct {
    uint32_t address, backdrop, handles[13];
    int16_t bounds[4], saved_vis[4];
    uint16_t proc_id;
    uint32_t palette_handle;
    uint16_t palette_updates;
    uint8_t title[256];
    int visible, active, updating, owns_storage;
} MacWindowState;

typedef struct {
    MacCpu *cpu;
    MacResourceFork fork;
    uint32_t a5, jump_table, jump_size, heap_next, heap_limit, heap_high, heap_high_end, stack_base;
    uint64_t instructions, traps;
    uint16_t last_trap;
    uint32_t last_trap_pc;
    uint32_t trap_addresses[4096];
    uint32_t qd_globals, current_port, screen_base;
    uint16_t screen_width, screen_height, screen_row_bytes, screen_depth;
    uint32_t palette[256];
    int16_t qd_error;
    uint32_t ticks;
    uint64_t clock_us;
    struct MacTimeState *time_manager;
    uint8_t keys[16]; /* Current supported host virtual-key state, independent of queue. */
    MacEvent events[64];
    unsigned event_head, event_count;
    int16_t mouse_x, mouse_y;
    uint16_t modifiers;
    uint16_t sane_environment;
    uint32_t manager_flags, wm_port, menu_bar, dialog_resume;
    void *file_state;
    struct MacResourceChain *resource_chain;
    struct MacSoundState *sound;
    struct MacSystemVolume *system_volume;
    struct MacDriveQueue *drive_queue;
    uint32_t main_device, current_device, screen_pixmap, screen_colors;
    int indexed_requested;
    uint8_t cursor[68];
    int cursor_valid, cursor_obscured;
    uint8_t dialog_params[4][256];
    uint32_t vbl_tasks[32], vbl_last_tick, vbl_due[32];
    unsigned vbl_count, vbl_due_count, vbl_due_cursor;
    int vbl_active;
    MacHandle handles[MAC_RUNTIME_HANDLES];
    size_t handle_count;
    MacWindowState window;
    MacWindowState parked_windows[7];
    uint32_t window_order[8];
    unsigned window_count;
    int exited;
    int trace;
    int guard_allocs;
    /* Extension-registered Gestalt selectors (NewGestalt). proc is recorded,
     * never called; the response is value. See mac_init.h. */
    uint32_t gestalt_sel[MAC_GESTALT_MAX], gestalt_val[MAC_GESTALT_MAX], gestalt_proc[MAC_GESTALT_MAX];
    unsigned gestalt_count;
    int init_guard, init_violation; /* INIT sandbox: protected traps refuse patches */
} MacRuntime;

/* Configure default guest RAM partition (2..12 MiB; 0 = 8 MiB baseline) and
 * 16-byte heap guard redzones for subsequent mac_runtime_init calls. */
void mac_runtime_set_memory_policy(int heap_mb, int guard_allocs);

/* The fork's backing bytes must outlive the runtime. This is a development
 * runtime, not a sandbox or a claim of complete Macintosh compatibility. */
int mac_runtime_init(MacRuntime *rt, const MacResourceFork *fork, char *err, size_t n);
void mac_runtime_destroy(MacRuntime *rt);
size_t mac_runtime_run(MacRuntime *rt, size_t instructions);
size_t mac_runtime_run_cycles(MacRuntime *rt, uint64_t cycles);
uint32_t mac_runtime_alloc(MacRuntime *rt, uint32_t size);
uint32_t mac_runtime_resource(MacRuntime *rt, uint32_t type, int16_t id);
int mac_runtime_trap(MacCpu *cpu, uint16_t opcode, uint32_t pc, void *user);
int mac_memory_service(MacRuntime *rt, uint16_t opcode);
int mac_resource_service(MacRuntime *rt, uint16_t opcode);
int mac_runtime_load_segment(MacRuntime *rt, int16_t id);
void mac_traps_init(MacRuntime *rt);
int mac_quickdraw_service(MacRuntime *rt, uint16_t opcode);
int mac_event_service(MacRuntime *rt, uint16_t opcode);
void mac_runtime_post_event(MacRuntime *rt, uint16_t what, uint32_t message,
                            uint16_t modifiers, int16_t x, int16_t y);
int mac_runtime_window(MacRuntime *rt, const char *title, size_t budget);
int mac_runtime_bridge_window(MacRuntime *rt, size_t budget, RuntimeBridge *bridge);
int mac_palette_manager_service(MacRuntime *rt, uint16_t opcode);
int mac_picture_service(MacRuntime *rt, uint16_t opcode);
int mac_sane_service(MacRuntime *rt, uint16_t opcode);
int mac_menu_service(MacRuntime *rt, uint16_t opcode);
int mac_toolbox_service(MacRuntime *rt, uint16_t opcode);
int mac_raster_service(MacRuntime *rt, uint16_t opcode);
int mac_system_service(MacRuntime *rt, uint16_t opcode);
int mac_env_service(MacRuntime *rt, uint16_t opcode);
int mac_env_implemented(unsigned key); /* trap_key() values */
/* Host-side Gestalt registration. 0 ok, -1 duplicate/full. */
int mac_gestalt_set(MacRuntime *rt, uint32_t selector, uint32_t value, uint32_t proc);
int mac_geometry_service(MacRuntime *rt, uint16_t opcode);
int mac_files_root(MacRuntime *rt, const char *directory);
void mac_files_destroy(MacRuntime *rt);
/* Mount a distinct writable save directory as an overlay on the data volume,
 * bounded by file-count and byte quotas. Without it the volume is read-only. */
int mac_files_writable(MacRuntime *rt, const char *directory, unsigned max_files, uint64_t max_bytes);
int mac_volume_blocks(int directory_fd, uint32_t *blocks);
int mac_volume_info(MacRuntime *rt, uint32_t pb, int directory_fd);
int mac_file_service(MacRuntime *rt, uint16_t opcode);
int mac_color_init(MacRuntime *rt);
int mac_color_service(MacRuntime *rt, uint16_t opcode);
int mac_cursor_service(MacRuntime *rt, uint16_t opcode);
int mac_dialog_service(MacRuntime *rt, uint16_t opcode);
int mac_color_port_service(MacRuntime *rt, uint16_t opcode);
int mac_copybits_service(MacRuntime *rt, uint16_t opcode);
int mac_palette_match_mask(MacRuntime *rt, uint8_t allowed[256]);
int mac_palette_service(MacRuntime *rt, uint16_t opcode);
int mac_color_pen_service(MacRuntime *rt, uint16_t opcode);
int mac_color_raster_service(MacRuntime *rt, uint16_t opcode);
int mac_video_config_service(MacRuntime *rt, uint16_t opcode);
int mac_pixels_service(MacRuntime *rt, uint16_t opcode);
int mac_pixmap_service(MacRuntime *rt, uint16_t opcode);
int mac_vbl_service(MacRuntime *rt, uint16_t opcode);
size_t mac_vbl_poll(MacRuntime *rt, size_t budget);

int mac_regions_service(MacRuntime *rt, uint16_t opcode);
int mac_window_resource_service(MacRuntime *rt, uint16_t opcode);
MacWindowState *mac_window_find(MacRuntime *rt, uint32_t address);
uint32_t mac_window_front(MacRuntime *rt);
int mac_window_service(MacRuntime *rt, uint16_t opcode);
int mac_window_update_event(MacRuntime *rt, uint16_t mask, MacEvent *out);

#endif
