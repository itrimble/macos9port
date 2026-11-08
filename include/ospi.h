#ifndef OSPI_INCLUDED
#define OSPI_INCLUDED

#include <stddef.h>
#include <stdint.h>
#include "system_icon_positions.h"

enum { OSPI_W = 1024, OSPI_H = 768 };
enum { TITLE_H = 20, INFO_H = 18, CELL_W = 120, CELL_H = 74, SB = 16 };
enum {
    OSPI_MAX_WIN = 8,
    OSPI_MAX_ITEMS = 48,
    OSPI_FOLDER_LIMIT = 32768,
    OSPI_CLIPBOARD_LIMIT = 32768 * 96,
    OSPI_SPECIAL = 24,
    OSPI_NAME = 96,
    OSPI_PATH = 640,
    OSPI_TEXT = 4096
};
/* Modifier bits retain the terminal input contract. */
enum { MOD_CMD = 1, MOD_SHIFT = 2, MOD_OPTION = 8, MOD_OTHER = 16 };
enum { WIN_FOLDER = 1, WIN_TEXT = 2, WIN_TERMINAL = 3, WIN_MOVIE = 4 };
enum { ITEM_FILE = 1, ITEM_FOLDER = 2, ITEM_SYSTEM = 3, ITEM_APPLICATION = 4, ITEM_ALIAS = 5 };
enum {
    CMD_NONE = 0,
    CMD_ABOUT,
    CMD_SHUTDOWN,
    CMD_NEW_FOLDER,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_QUIT,
    CMD_EMPTY_ASK,
    CMD_EMPTY_DO,
    CMD_CLEANUP,
    CMD_HELP,
    CMD_INSTALL,
    CMD_INSTALL_DO,
    CMD_STARTUP_TOGGLE,
    CMD_STARTUP_PREVIEW,
    CMD_SOUND_TOGGLE,
    CMD_BROWSER,
    CMD_TERMINAL_SHELL,
    CMD_TERMINAL_CONSOLE,
    CMD_PASTE, /* CMD_COPY is the shared 422 id below; the terminal handles it when front. */
    CMD_TERM_CLEAR,
    CMD_TERM_THEME,
    CMD_TERM_PASTE_DO, CMD_TAILSCALE, CMD_QUAKE3, CMD_QUAKE3_DEMO
};
/* Theme commands are separate from desktop picture commands. */
enum { CMD_THEME_BASE = 200, CMD_THEME_LAST = 215, CMD_THEME_NEXT = 216 };
enum { CMD_WALLPAPER_BASE = 300, CMD_WALLPAPER_LAST = 359,
       CMD_WALLPAPER_PREVIOUS = 400, CMD_WALLPAPER_NEXT = 401,
       CMD_APPEARANCE_DESKTOP = 410, CMD_CONTROL_PANELS = 411,
       CMD_APPEARANCE_THEMES = 412, CMD_CP_GENERAL = 413, CMD_CP_SOUND = 414,
       CMD_CP_MONITORS = 415, CMD_CP_KEYBOARD = 416, CMD_CP_ENERGY = 417,
       CMD_CP_MEMORY = 418, CMD_CP_NETWORK = 419,
       MENU_APPEARANCE = 6, WALLPAPERS_PER_PAGE = 10 };
enum { CMD_GET_INFO = 420, CMD_SELECT_ALL = 421, CMD_COPY = 422, CMD_CLOSE_ALL = 423, CMD_STANDARD_ICON = 424,
       CMD_VIEW_ICONS = 430, CMD_VIEW_LIST = 431, CMD_SORT_NAME = 432, CMD_SORT_KIND = 433,
       CMD_HELP_KEYS = 440, CMD_HELP_STATUS = 441 };
enum { CMD_SHOW_CLIPBOARD = 450, CMD_PREFERENCES, CMD_VIEW_OPTIONS,
       CMD_SORT_DATE, CMD_SORT_SIZE, CMD_VIEW_BUTTONS, CMD_AS_WINDOW, CMD_AS_POPUP,
       CMD_RESTART, CMD_RESTART_DO, CMD_FIND, CMD_FIND_DO, CMD_FIND_AGAIN,
       CMD_DUPLICATE, CMD_RENAME, CMD_RENAME_DO, CMD_MAKE_ALIAS, CMD_SHOW_ORIGINAL,
       CMD_FAVORITE, CMD_PUT_AWAY, CMD_MOVE_TRASH, CMD_UNDO, CMD_CLEAR,
       CMD_PRINT, CMD_PAGE_SETUP, CMD_PRINT_WINDOW, CMD_EJECT, CMD_ERASE, CMD_SLEEP,
       CMD_ENCRYPT, CMD_LABEL, CMD_RESET_COLUMNS, CMD_FAVORITES, CMD_RECENT,
       CMD_SETTINGS_APPLY = 490, CMD_SORT_CREATED, CMD_SORT_LABEL, CMD_EDIT_LABELS, CMD_LABELS_APPLY, CMD_DESKTOP_ITEMS, CMD_WINDOW_BASE = 500 };
enum { VIEW_ICONS = 0, VIEW_LIST = 1, VIEW_BUTTONS = 2,
       SORT_NAME = 0, SORT_KIND = 1, SORT_DATE = 2, SORT_SIZE = 3, SORT_CREATED = 4, SORT_LABEL = 5 };
enum { MI_DISABLED = 1, MI_CHECKED = 2, MI_SUBMENU = 4 };
enum { MENU_ARRANGE = 8, MENU_LABELS = 9, MENU_PANELS = 10, MENU_RECENT_APPS = 11, MENU_RECENT_DOCS = 12,
       CMD_MENU_ARRANGE = 600, CMD_MENU_LABELS, CMD_MENU_PANELS,
       CMD_MENU_RECENT_APPS = 604, CMD_MENU_RECENT_DOCS = 605,
       CMD_LABEL_BASE = 620, CMD_RECENT_BASE = 640, CMD_RECENT_CLEAR_APPS = 670, CMD_RECENT_CLEAR_DOCS = 671,
       CMD_APP_FINDER = 680, CMD_APP_GUEST, CMD_APP_HIDE, CMD_APP_HIDE_OTHERS, CMD_APP_SHOW_ALL };
enum { CMD_APP_MEDIA = 685, CMD_APP_BROWSER = 686, CMD_APP_SWITCHER = 687 };
enum { BROWSER_ACTIVATE = 1, BROWSER_CLOSE, BROWSER_CLOSE_ALL, BROWSER_QUIT, BROWSER_NEW_WINDOW,
       BROWSER_COPY, BROWSER_PASTE, BROWSER_SELECT_ALL, BROWSER_UNDO };
enum { CMD_MEDIA_PLAYER = 700, CMD_SUPPORTED_FORMATS = 701 };
enum { BROWSER_CAN_COPY = 1, BROWSER_CAN_PASTE = 2, BROWSER_CAN_SELECT = 4, BROWSER_CAN_UNDO = 8 };

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    const char *label;
    int cmd;
    int flags;
    const char *shortcut;
    int key;
} MenuItem;

typedef struct {
    char name[OSPI_NAME];
    int kind;
    int selected;
    char location[OSPI_PATH]; /* Find results retain their volume-relative path. */
    uint64_t data_size;
    int64_t modified, created; /* created == 0 when host birth time is unavailable. */
    int label;
    int desktop_x, desktop_y, desktop_position;
    uint8_t guest_icon[1024];
    uint8_t guest_icon_mask[128];
    int has_guest_icon;
    char special[OSPI_SPECIAL];   /* a special folder's icon, kept through renames */
} Item;

typedef struct {
    int kind;
    int desktop_surface;
    int x, y, w, h;
    int zoom_x, zoom_y, zoom_w, zoom_h;
    int shade_h;
    int zoomed;
    int collapsed;
    int scroll;
    int view_mode, sort_mode, sort_reverse;
    int list_columns[3]; /* Divider positions in thousandths of content width. */
    int selection_anchor, selection_cursor;
    int search_results;
    uint64_t folder_stamp;
    int folder_stamp_valid;
    int popup, popup_x, popup_y, popup_w, popup_h, popup_tab_x;
    char title[OSPI_NAME];
    char rel[OSPI_PATH];
    Item items[OSPI_MAX_ITEMS];
    Item *expanded_items;
    int item_capacity, listing_limited;
    int nitems;
    char text[OSPI_TEXT];
    char *expanded_text;
    int note_editable, note_select_all;
    size_t note_caret;
    struct TermSession *term;
    void *movie;
    int movie_modern;
} Window;

/* Small fixtures/windows retain inline storage; large folders own their buffer. */
#define window_text(w) ((w)->expanded_text ? (w)->expanded_text : (w)->text)
#define window_items(w) ((w)->expanded_items ? (w)->expanded_items : (w)->items)
static inline int window_item_capacity(const Window *w) {
    return w->expanded_items ? w->item_capacity : OSPI_MAX_ITEMS;
}

typedef struct {
    Rect outer, title, info, content, closeb, shade, zoom, vbar, hbar, grow;
} Layout;

enum { OSPI_FOLDER_PREFS = 64 };
typedef struct {
    char rel[OSPI_PATH];
    int x, y, w, h, view, sort, scroll;
    int open, popup, sort_reverse, list_columns[3];
} FolderPreference;
typedef struct {
    int type, identity_valid;
    uint64_t device, inode, fork_device, fork_inode;
    int file_kind, fork_present;
    char parent[OSPI_PATH], destination[OSPI_PATH], name[OSPI_NAME], original[OSPI_NAME];
} FinderUndo;
typedef struct {
    char path[OSPI_PATH];
    int application;
} RecentItem;
typedef struct Session Session;

struct Session {
    char root[OSPI_PATH];
    Window win[OSPI_MAX_WIN];
    Window desktop;
    SystemIconPositions system_icons;
    int nwin;
    FolderPreference folder_prefs[OSPI_FOLDER_PREFS];
    int nfolder_prefs;
    int menu;
    RecentItem recent[16];
    int nrecent;
    int guest_active, guest_focused, guest_hidden, finder_hidden, app_request;
    int app_switch_tab;
    int app_order[4];
    struct { int open, x, y, width, small, no_names, horizontal, collapsed;
             int gesture, pressed, dx, dy, tear_armed; } switcher;
    int media_focused, media_hidden;
    int browser_active, browser_focused, browser_hidden, browser_action;
    int browser_windows, browser_edit_flags;
    char guest_title[OSPI_NAME];
    Rect guest_bounds;
    int (*guest_menu_command)(int command);
    int print_available, print_request;
    Rect print_rect;
    char print_title[OSPI_NAME], print_text[65536];
    int menu_hot;
    int menu_child, menu_child_hot;
    int desk_sel;
    int desktop_focus; /* Background and volume icons own Finder commands. */
    int quit;
    uint64_t free_bytes;
    uint64_t ram;
    char arch[16];
    uint32_t last_stat;
    uint32_t input_now_ms, click_ms;
    int click_valid, click_kind, click_x, click_y;
    char click_dir[OSPI_PATH], click_name[OSPI_NAME];
    int sw, sh;
    int mx, my;
    int down;
    int press_x, press_y, press_clicks;
    int press_win;
    int press_zone;
    int press_item;
    int mouse_mods, press_selection_keep;
    uint8_t selection_before[OSPI_FOLDER_LIMIT];
    int drag;
    int spring_disabled, spring_delay_ms;
    struct { int active, count, made, hover, desktop_source, collapsed[OSPI_MAX_WIN]; uint32_t since;
             char source[OSPI_PATH], target[OSPI_PATH];
             char paths[OSPI_MAX_WIN][OSPI_PATH], created[OSPI_MAX_WIN][OSPI_PATH]; } spring;
    int orig_x, orig_y, orig_w, orig_h;
    int dialog;
    int dialog_cmd;
    int dialog_down;
    int settings_kind, settings_row, settings_flags, settings_view, settings_sort;
    int settings_delay;
    char find_query[OSPI_NAME];
    char edit_text[OSPI_NAME];
    int edit_selected;
    char edit_parent[OSPI_PATH], edit_name[OSPI_NAME];
    FinderUndo undo_entries[OSPI_MAX_ITEMS];
    FinderUndo *expanded_undo;
    int undo_capacity;
    int undo_count;
    char dialog_title[64];
    char dialog_body[384];
    char clip[512];
    char install_url[512];
    int startup_toggle_request;
    int startup_preview_request;
    int sound_toggle_request;
    int sound_preview_request;
    int host_sound_enabled;
    int host_startup_enabled;
    char clipboard_in[OSPI_TEXT];
    char clipboard_out[OSPI_TEXT];
    char *clipboard_large_in, *clipboard_large_out;
    int clipboard_read_error;
    int clipboard_write_request;
    char sound_event[48];
    int browser_request;
    char browser_open_url[256];
    /* Finder name editing and keyboard navigation (finder_edit.c). */
    int edit_on, edit_len, edit_caret, edit_anchor, edit_armed, edit_cand;
    uint32_t edit_since, edit_arm_ms, type_ms;
    char edit_rel[OSPI_PATH], edit_old[OSPI_NAME], edit_buf[OSPI_NAME];
    char type_buf[32];
    int clip_out;
    char info_rel[OSPI_PATH];
    void *cp_store;
    int cursor_blink_ms, cursor_blink_set, menu_blink_count, desktop_pattern;
    int menu_flash_menu, menu_flash_row;
    uint32_t menu_flash_start_ms, menu_flash_end_ms;
    int system_volume, sound_muted, startup_chime_enabled, volume_sync_request, startup_sync_request;
    char alert_sound[32];
    int display_color_mode, display_brightness, display_gamma, display_scanlines, display_integer_scale;
    int key_repeat_rate, key_repeat_delay_ms, double_click_ms, wheel_speed, natural_scroll;
    int held_key_active, held_key, held_mods;
    uint32_t held_since_ms, held_last_repeat_ms;
    char held_text[8];
    int energy_sleep_mins, power_profile, energy_dim_before_sleep, display_dimmed;
    int energy_prevent_guest_sleep, energy_wake_on_net, guest_running;
    uint32_t last_activity_ms;
    int disk_cache_kb, guest_ram_mb, guest_guard_pages, ram_disk_mb, ram_disk_clear_on_quit;
    int net_connect_via, net_https_only, net_allow_redirects, net_guest_isolated, net_timeout_sec;
};

typedef struct {
    int (*is_media_file)(const char *full_path);
    void (*open_file)(Session *s, const char *full_path);
    void (*open_panel)(Session *s, int selected_tab);
    int (*active)(void);
    void (*close)(void);
    void (*draw)(Session *s, void *fb);
    int (*down)(Session *s, int x, int y);
    int (*move)(Session *s, int x, int y);
    int (*up)(Session *s, int x, int y);
    int (*key)(Session *s, int key, int mods);
    int (*bounds)(const Session *s, Rect *out);
    int (*running)(const Session *s);
    void (*activate)(Session *s);
    void (*quit)(Session *s);
    int (*try_open_file)(Session *s, const char *full_path); /* Zero on success. */
} SessionMediaHooks;

void session_set_media_hooks(const SessionMediaHooks *hooks);
const SessionMediaHooks *session_media_hooks(void);

enum {
    Z_NONE = 0,
    Z_TITLE,
    Z_CLOSE,
    Z_SHADE,
    Z_ZOOM,
    Z_GROW,
    Z_ITEM,
    Z_CONTENT,
    Z_COLUMN,
    Z_DESKTOP
};

/* Desktop item gestures use a stable sentinel, independent of window ordering. */
#define OSPI_DESKTOP_WINDOW OSPI_MAX_WIN
static inline Window *finder_window(const Session *s, int index) {
    if (!s) return NULL;
    if (index == OSPI_DESKTOP_WINDOW) return (Window *)&s->desktop;
    return index >= 0 && index < s->nwin ? (Window *)&s->win[index] : NULL;
}
static inline Window *finder_current(const Session *s) {
    if (!s || s->desk_sel >= 0) return NULL;
    if (s->desktop_focus || !s->nwin) return s->desktop.kind == WIN_FOLDER ? (Window *)&s->desktop : NULL;
    return s->nwin > 0 && s->nwin <= OSPI_MAX_WIN ? (Window *)&s->win[s->nwin - 1] : NULL;
}

void window_layout(const Window *w, Layout *lay);
int window_max_scroll(const Window *w);
int rect_has(Rect r, int x, int y);
int item_index_at(const Window *w, int x, int y);
Rect desktop_item_cell(const Window *w, int index);
int desktop_item_visible(const Window *w, int index);
void desktop_icon_rect(int sw, int sh, int which, Rect *icon, Rect *hit);
void desktop_position_rect(int sw, int sh, int x, int y, Rect *icon, Rect *hit);
void session_desktop_icon_rect(const Session *s, int which, Rect *icon, Rect *hit);
int session_desktop_which(const Session *s, int x, int y);
int desktop_which(int sw, int sh, int x, int y);
void dialog_layout(const Session *s, Rect *box, Rect *cancel, Rect *ok);

void menu_update(Session *s);
void menu_set_sound_enabled(int enabled);
void menu_wallpaper_page(int page);
int menu_wallpaper_current_page(void);
int menu_count(void);
const char *menu_title(int index);
int menu_nitems(int menu);
const MenuItem *menu_at(int menu, int item);
void menu_title_rect(int index, Rect *r);
void menu_item_rect(int menu, int item, Rect *r);
int menu_title_at(int x, int y);
int menu_item_at(int menu, int x, int y);
int menu_cmd_for_key(int key);
int menu_submenu(int command);
void menu_hover(Session *s, int x, int y);
int menu_activate(Session *s, int menu, int row);

int text_height(const char *text, int max_w);
void text_draw(void *fb, int x, int y, int max_w, uint32_t color, const char *text);

int volume_builtin_tailnet(const char *root, const char *rel);
int volume_builtin_terminal(const char *root, const char *rel);
int volume_seed(const char *root);
int volume_mkdir_p(const char *path);
int volume_free_bytes(const char *root, uint64_t *out);
int volume_list(const char *root, const char *rel, Item *items, int max);
int volume_list_dynamic(const char *root, const char *rel, Item **items, int *limited);
int volume_read(const char *root, const char *rel, char *buf, size_t n);
int volume_mkdir_unique(const char *root, const char *rel, const char *base,
                        char *created, size_t created_n);
int volume_trash(const char *root, const char *rel, const char *name);
int volume_count(const char *root, const char *rel);
int volume_empty_trash(const char *root);
int volume_ram_disk_sync(const char *root, int enabled, int quota_mb, int wipe_contents);
int volume_ram_disk_usage(const char *root, uint64_t *used_bytes, int *quota_mb);
void volume_disk_cache_set_kb(int kb);
void volume_disk_cache_stats(size_t *used_bytes, size_t *cap_bytes, unsigned *hits);
int volume_join(char *out, size_t n, const char *root, const char *rel);

uint64_t host_ram(void);
const char *host_arch(void);

Session *session_create(const char *root);
void session_destroy(Session *s);
void session_paint(Session *s, uint32_t *px, int w, int h);
int session_overlay_active(const Session *s);
void session_windows_paint_above(Session *s, uint32_t *px, int w, int h, int app);
void session_windows_paint(Session *s, uint32_t *px, int w, int h);
void session_overlay_paint(Session *s, uint32_t *px, int w, int h);
void session_context(Session *s, int x, int y);
void session_down(Session *s, int x, int y, int clicks);
void session_up(Session *s, int x, int y);
void session_move(Session *s, int x, int y);
void session_wheel(Session *s, int dy);
void session_key(Session *s, int key, int mods);
void session_key_hold(Session *s, int key, int mods, const char *utf8, uint32_t now_ms);
void session_key_release(Session *s, int key);
void session_note_network_activity(Session *s);
void session_text_input(Session *s, const char *text);
void session_tick(Session *s, uint32_t now_ms);
int session_quit(const Session *s);
void session_command(Session *s, int cmd);
int session_take_startup_toggle(Session *s);
int session_take_startup_preview(Session *s);
void session_clamp(Session *s);
#define session_clipboard_output(s) ((s)->clipboard_large_out ? (s)->clipboard_large_out : (s)->clipboard_out)
#define session_clipboard_input(s) ((s)->clipboard_large_in ? (s)->clipboard_large_in : (s)->clipboard_in)
void session_set_clip(Session *s, const char *text);
/* Text typed while an icon name is being edited (UTF-8, no control keys). */
void session_text(Session *s, const char *utf8);
int session_editing(const Session *s);
/* The name box being edited, in desktop pixels; 0 when not editing. */
int session_edit_rect(const Session *s, Rect *r);
/* Start editing the name under (x, y), as a Force Click on a name does. */
int session_rename_at(Session *s, int x, int y);
/* 1 once after Copy or Cut put new text in s->clip for the host clipboard. */
int session_take_clip_out(Session *s);
int volume_rename(const char *root, const char *rel, const char *from, const char *to);
/* Special folders (special_folder.c): the icon a folder keeps after renaming,
 * and FindFolder-style lookup of a role such as "applications_folder". */
int volume_folder_icon(const char *root, const char *rel, char *out, size_t n);
int volume_clear_folder_icon(const char *root, const char *rel);
int volume_set_folder_icon(const char *root, const char *rel, const char *icon);
int volume_special_folder(const char *root, const char *icon, const char *name, char *rel, size_t n);

int garden_page_ok(const char *url);
int garden_pick_url(const char *html, char *out, size_t n);
int garden_alt_url(const char *url, int which, char *out, size_t n);
int install_garden(const char *root, const char *page_url, char *created_rel, size_t created_n,
                   char *err, size_t err_n);
int install_archive_supported(const char *name);
int install_archive(const char *root, const char *archive_rel, char *created_rel, size_t created_n,
                    char *err, size_t err_n);
int install_sit(const char *root, const char *archive_rel, char *created_rel, size_t created_n,
                char *err, size_t err_n);
int classic_is_program(const char *name);
int classic_bundle(const char *root, const char *full, const char *name, char *launch, size_t launch_n,
                   char *hint, size_t hint_n);
int classic_launch(const char *path, const char *hint, char *err, size_t err_n);
int classic_open(const char *root, const char *full, const char *name, char *err, size_t err_n);

int ospi_item_center(const Session *s, int win, int index, int *x, int *y);
int ospi_control_center(const Window *w, int which, int *x, int *y);
int ospi_desktop_center(const Session *s, int which, int *x, int *y);
int ospi_menu_center(int menu, int item, int *x, int *y);
int ospi_dialog_center(const Session *s, int which, int *x, int *y);

#endif
