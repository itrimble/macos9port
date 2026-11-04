#ifndef OSPI_INCLUDED
#define OSPI_INCLUDED

#include <stddef.h>
#include <stdint.h>

enum { OSPI_W = 1024, OSPI_H = 768 };
enum { TITLE_H = 20, INFO_H = 18, CELL_W = 120, CELL_H = 74, SB = 16 };
enum {
    OSPI_MAX_WIN = 8,
    OSPI_MAX_ITEMS = 48,
    OSPI_NAME = 96,
    OSPI_PATH = 640,
    OSPI_TEXT = 4096
};
enum { MOD_CMD = 1 };
enum { WIN_FOLDER = 1, WIN_TEXT = 2 };
enum { ITEM_FILE = 1, ITEM_FOLDER = 2, ITEM_SYSTEM = 3 };
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
    CMD_HELP
};
enum { MI_DISABLED = 1, MI_CHECKED = 2 };

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
} Item;

typedef struct {
    int kind;
    int x, y, w, h;
    int zoom_x, zoom_y, zoom_w, zoom_h;
    int shade_h;
    int zoomed;
    int collapsed;
    int scroll;
    char title[OSPI_NAME];
    char rel[OSPI_PATH];
    Item items[OSPI_MAX_ITEMS];
    int nitems;
    char text[OSPI_TEXT];
} Window;

typedef struct {
    Rect outer, title, info, content, closeb, shade, zoom, vbar, hbar, grow;
} Layout;

typedef struct Session Session;

struct Session {
    char root[OSPI_PATH];
    Window win[OSPI_MAX_WIN];
    int nwin;
    int menu;
    int menu_hot;
    int desk_sel;
    int quit;
    uint64_t free_bytes;
    uint64_t ram;
    char arch[16];
    uint32_t last_stat;
    int sw, sh;
    int mx, my;
    int down;
    int press_x, press_y, press_clicks;
    int press_win;
    int press_zone;
    int press_item;
    int drag;
    int orig_x, orig_y, orig_w, orig_h;
    int dialog;
    int dialog_cmd;
    int dialog_down;
    char dialog_title[64];
    char dialog_body[384];
};

enum {
    Z_NONE = 0,
    Z_TITLE,
    Z_CLOSE,
    Z_SHADE,
    Z_ZOOM,
    Z_GROW,
    Z_ITEM,
    Z_CONTENT
};

void window_layout(const Window *w, Layout *lay);
int window_max_scroll(const Window *w);
int rect_has(Rect r, int x, int y);
int item_index_at(const Window *w, int x, int y);
void desktop_icon_rect(int sw, int sh, int which, Rect *icon, Rect *hit);
int desktop_which(int sw, int sh, int x, int y);
void dialog_layout(const Session *s, Rect *box, Rect *cancel, Rect *ok);

int menu_count(void);
const char *menu_title(int index);
int menu_nitems(int menu);
const MenuItem *menu_at(int menu, int item);
void menu_title_rect(int index, Rect *r);
void menu_item_rect(int menu, int item, Rect *r);
int menu_title_at(int x, int y);
int menu_item_at(int menu, int x, int y);
int menu_cmd_for_key(int key);

int text_height(const char *text, int max_w);
void text_draw(void *fb, int x, int y, int max_w, uint32_t color, const char *text);

int volume_seed(const char *root);
int volume_mkdir_p(const char *path);
int volume_free_bytes(const char *root, uint64_t *out);
int volume_list(const char *root, const char *rel, Item *items, int max);
int volume_read(const char *root, const char *rel, char *buf, size_t n);
int volume_mkdir_unique(const char *root, const char *rel, const char *base,
                        char *created, size_t created_n);
int volume_trash(const char *root, const char *rel, const char *name);
int volume_count(const char *root, const char *rel);
int volume_empty_trash(const char *root);
int volume_join(char *out, size_t n, const char *root, const char *rel);

uint64_t host_ram(void);
const char *host_arch(void);

Session *session_create(const char *root);
void session_destroy(Session *s);
void session_paint(Session *s, uint32_t *px, int w, int h);
void session_down(Session *s, int x, int y, int clicks);
void session_up(Session *s, int x, int y);
void session_move(Session *s, int x, int y);
void session_wheel(Session *s, int dy);
void session_key(Session *s, int key, int mods);
void session_tick(Session *s, uint32_t now_ms);
int session_quit(const Session *s);
void session_command(Session *s, int cmd);
void session_clamp(Session *s);

int ospi_item_center(const Session *s, int win, int index, int *x, int *y);
int ospi_control_center(const Window *w, int which, int *x, int *y);
int ospi_desktop_center(const Session *s, int which, int *x, int *y);
int ospi_menu_center(int menu, int item, int *x, int *y);
int ospi_dialog_center(const Session *s, int which, int *x, int *y);

#endif
