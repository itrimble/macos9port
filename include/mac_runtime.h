#ifndef MAC_RUNTIME_H
#define MAC_RUNTIME_H

#include "mac_cpu.h"
#include "mac_resource.h"
#include "runtime_bridge.h"
/* Completion of a guest call started by mac_guest_call: `rt` is the MacRuntime, `result` the routine's return value
 * (0 if none). Return < 0 to halt. It may start another guest call; otherwise execution resumes after the trap. */
typedef int (*MacGuestDone)(void *rt, void *ctx, uint32_t result);

#define MAC_CODE_TYPE UINT32_C(0x434f4445)
#define MAC_RUNTIME_RAM (16u * 1024u * 1024u)
#define MAC_RUNTIME_HANDLES 512
/* Guest compatibility profile (Ian, 2026-11-07): the virtual machine presents itself as a Mac II-class
 * computer (68020, indexed Color QuickDraw, no FPU) running System 7.0. It is an identity for applications
 * that gate on SysEnvirons/Gestalt, not a claim that every System 7 service exists: unsupported traps and
 * Gestalt selectors still halt or return gestaltUndefSelectorErr. */
#define MAC_PROFILE_SYSTEM_VERSION 0x0700 /* Gestalt 'sysv', SysEnvirons systemVersion, low-memory SysVersion */
#define MAC_PROFILE_SYSENV_MACHINE 4      /* SysEnvirons envMacII */
#define MAC_PROFILE_GESTALT_MACHINE 6     /* Gestalt 'mach' gestaltMacII */
/* Smallest poll budget the VBL/Time Manager/sound pollers accept. */
#define MAC_RUNTIME_SERVICE_MIN 10000u

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
    /* 'wctb' overrides applied at creation. Bit n of color_set: part n is overridden (0 content-region background,
     * 1 window frame, 2 title text); color_rgb[n] holds its 16-bit RGB, matched to the nearest palette entry at draw
     * time so palette changes (e.g. the gray mode) stay correct. Zero = the native default colors. */
    uint8_t color_set;
    uint16_t color_rgb[3][3];
} MacWindowState;

typedef struct MacHeapBridge MacHeapBridge;

/* Apple Event Manager state ($A816). Handlers are only RECORDED; nothing in this runtime sends an Apple Event, so
 * none is ever called. Two separate tables (application and system) of bounded size, then the interaction level. */
#define MAC_AE_HANDLERS 64
typedef struct { uint32_t event_class, event_id, handler, refcon; } MacAeHandler;
typedef struct {
    MacAeHandler app[MAC_AE_HANDLERS], sys[MAC_AE_HANDLERS];
    unsigned app_count, sys_count;
    uint8_t level, level_set; /* AEInteractAllowed 0..2; level_set == 0 means never set (reported as the default) */
} MacAppleEvents;
struct MacResEdits;

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
    uint32_t hier_menus[32]; /* submenus registered with InsertMenu(menu, -1); never drawn in the bar */
    unsigned hier_count;
    struct MacResEdits *res_edits; /* resource edits layered over the fork (host memory) */
    uint8_t palette_map[256];  /* device index chosen for each entry of the activated palette */
    uint32_t palette_active;   /* handle of the palette last activated, 0 = none */
    MacHeapBridge *heap_bridge; /* protected heap over [lo,hi) of guest RAM; NULL = legacy bump only */
    MacHandle handles[MAC_RUNTIME_HANDLES];
    size_t handle_count;
    MacWindowState window;
    MacWindowState parked_windows[7];
    uint32_t window_order[8];
    unsigned window_count;
    int exited;
    int trace;
    uint32_t appfork_mark; /* file mark of the application resource fork opened for direct reads (mac_appfork.c) */
    struct MacAlertState *alert; /* native alert dialog (mac_alert.c) */
    struct MacSfState *stdfile; uint32_t stdfile_pb; /* native Standard File dialog (mac_stdfile.c) */
    struct { int active; MacGuestDone done; void *ctx; uint32_t sp, resume_pc, result_bytes; } guest; /* mac_guestcall.c */
    struct { int active; uint32_t deadline, pc, sp; } delay; /* _Delay in progress (mac_delay.c); host memory only */
    MacAppleEvents appleevents; /* Apple Event Manager handler tables, host memory only (mac_appleevents.c) */
} MacRuntime;

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
void mac_memory_result(MacCpu *cpu, int16_t err);
/* Protected heap (os999_heap) over guest RAM [lo,hi); hi must lie within guest RAM.
 * The legacy bump allocator must not use that window. Detach is idempotent. */
/* Session edits to resources (SetResInfo names, WriteResource data) layered over the immutable fork.
 * Keyed by (owner, type, id); host memory only. Owner 0 is the application fork. */
int mac_resedit_set_name(MacRuntime *rt, int16_t owner, uint32_t type, int16_t id, const uint8_t *pascal);
int mac_resedit_set_data(MacRuntime *rt, int16_t owner, uint32_t type, int16_t id, const uint8_t *data, uint32_t size);
void mac_resedit_apply(const MacRuntime *rt, int16_t owner, MacResource *r);
/* Persist application-fork edits into the writable save overlay (hidden file bound to this exact fork).
 * 0 saved, 1 nothing to save or no overlay mounted (edits stay in this session), <0 File Manager error. */
int mac_resedit_save(MacRuntime *rt);
/* Load previously saved edits once the overlay is mounted; returns the number applied (a bad file applies none). */
int mac_resedit_load(MacRuntime *rt);
void mac_resedit_destroy(MacRuntime *rt);
/* Hidden (dot-named) overlay files: invisible to the guest, written via a staged copy and rename.
 * put: 0 ok, 1 no overlay, <0 error. get: 0 ok (caller frees *data), nonzero if absent or unreadable. */
int mac_files_overlay_put_hidden(MacRuntime *rt, const char *name, const uint8_t *data, size_t size);
int mac_files_overlay_get_hidden(MacRuntime *rt, const char *name, size_t limit, uint8_t **data, size_t *size);
int mac_heap_attach(MacRuntime *rt, uint32_t lo, uint32_t hi);
void mac_heap_detach(MacRuntime *rt);
int mac_heap_owns(const MacRuntime *rt, uint32_t address);
/* Runtime-owned, zeroed, non-relocatable storage in the protected heap (0 on failure);
 * guests cannot dispose or size it. mac_heap_free ignores addresses it does not own. */
uint32_t mac_heap_alloc(MacRuntime *rt, uint32_t size);
void mac_heap_free(MacRuntime *rt, uint32_t address);
/* Bulk data (resource bodies): protected heap when attached, else the bump allocator. */
uint32_t mac_runtime_alloc_data(MacRuntime *rt, uint32_t size);
void mac_runtime_free_data(MacRuntime *rt, uint32_t address);
int mac_heap_service(MacRuntime *rt, uint16_t opcode);
/* One-line usage summary for diagnostics (bump area and protected heap). */
void mac_heap_report(const MacRuntime *rt, char *out, size_t n);
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
int mac_numconv_service(MacRuntime *rt, uint16_t opcode);
int mac_help_service(MacRuntime *rt, uint16_t opcode); /* Help Manager dispatch, $A830 */
int mac_appleevents_service(MacRuntime *rt, uint16_t opcode); /* Apple Event Manager handler tables, $A816 */
/* Host-side lookup for a future dispatcher (application table, then system table; exact, class+'****', '****'+id,
 * '****'+'****'). Returns 1 and fills the outputs on a hit, 0 otherwise. Nothing in the runtime calls it yet. */
int mac_ae_find_handler(const MacRuntime *rt, uint32_t event_class, uint32_t event_id, uint32_t *handler, uint32_t *refcon);
/* Call guest code from inside a trap service (see mac_guestcall.c). Returns 0 once the call is set up (the service must
 * then return handled without touching PC or A7), -1 after halting. */
int mac_guest_call(MacRuntime *rt, uint32_t routine, const uint8_t *args, unsigned arg_bytes,
                   unsigned result_bytes, MacGuestDone done, void *ctx);
int mac_guest_return(MacRuntime *rt); /* trap handler hook: the routine returned to the call stub */
int mac_delay_service(MacRuntime *rt, uint16_t opcode); /* Time Manager _Delay, $A03B */
void mac_appfork_publish(MacRuntime *rt); /* CurApRefNum, CurMap, TopMapHndl */
int mac_appfork_service(MacRuntime *rt, uint16_t opcode); /* PBRead/PBGetEOF on the application resource file */
int mac_dialog_next_event(MacRuntime *rt, MacEvent *out); /* next mouse/key event for a modal dialog */
int mac_alert_service(MacRuntime *rt, uint16_t opcode); /* Alert, StopAlert, NoteAlert, CautionAlert: $A985..$A988 */
void mac_alert_destroy(MacRuntime *rt);
int mac_region_box_algebra(unsigned op, const int a[4], const int b[4], int out[4]); /* rectangle algebra, mac_regions.c */
int mac_window_inval_service(MacRuntime *rt, uint16_t opcode); /* InvalRect/InvalRgn/ValidRect/ValidRgn, $A927..$A92A */
int mac_stdfile_service(MacRuntime *rt, uint16_t opcode); /* Pack3 Standard File, $A9EA */
void mac_stdfile_destroy(MacRuntime *rt);
int mac_menu_service(MacRuntime *rt, uint16_t opcode);
int mac_toolbox_service(MacRuntime *rt, uint16_t opcode);
int mac_raster_service(MacRuntime *rt, uint16_t opcode);
int mac_system_service(MacRuntime *rt, uint16_t opcode);
int mac_geometry_service(MacRuntime *rt, uint16_t opcode);
int mac_files_root(MacRuntime *rt, const char *directory);
/* One entry of the merged guest volume listing (data files plus committed saves): host regular files only, sorted by
 * name (byte order), the Finder info the guest has set (zero when none; nothing is invented from the name). */
typedef struct { char name[256]; uint32_t type, creator; uint16_t flags; } MacFileEntry;
/* Fill up to `max` entries and set *count. Returns 0, or a File Manager error (-35 no volume, -36 I/O, -50 too many). */
int mac_files_entries(MacRuntime *rt, MacFileEntry *out, unsigned max, unsigned *count);
void mac_files_destroy(MacRuntime *rt);
/* Mount a distinct writable save directory as an overlay on the data volume,
 * bounded by file-count and byte quotas. Without it the volume is read-only. */
int mac_files_writable(MacRuntime *rt, const char *directory, unsigned max_files, uint64_t max_bytes);
int mac_volume_blocks(int directory_fd, uint32_t *blocks);
int mac_volume_info(MacRuntime *rt, uint32_t pb, int directory_fd);
int mac_volume_info_basic(MacRuntime *rt, uint32_t pb, int directory_fd); /* PBGetVInfo: only the 64-byte volumeParam */
int mac_file_service(MacRuntime *rt, uint16_t opcode);
int mac_fsspec_service(MacRuntime *rt, uint16_t opcode); /* HighLevelFSDispatch ($AA52): FSMakeFSSpec, FSpOpenDF, ... */
int mac_color_init(MacRuntime *rt);
/* Main device color/gray table switch at the current 8-bit depth (SetDepth's device-type bit). 0 ok. */
int mac_color_set_gray(MacRuntime *rt, int gray);
/* Palette activation: the main device's table back to its default for the current type, and single-entry writes. */
int mac_color_reset_table(MacRuntime *rt);
int mac_color_set_entry(MacRuntime *rt, unsigned index, uint16_t r, uint16_t g, uint16_t b);
/* Realize the palette associated with the frontmost visible window on the main device. 0 ok (or nothing to do),
 * -1 with the CPU halted when the palette needs something unsupported. */
int mac_palette_activate(MacRuntime *rt, MacWindowState *window);
/* The refusals activation would make, checked without changing anything (0 ok, -1 halted). */
int mac_palette_check(MacRuntime *rt, const MacWindowState *window);
int mac_color_service(MacRuntime *rt, uint16_t opcode);
int mac_cursor_service(MacRuntime *rt, uint16_t opcode);
int mac_ccursor_service(MacRuntime *rt, uint16_t opcode);
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
