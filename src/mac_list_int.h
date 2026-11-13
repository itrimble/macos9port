#ifndef MAC_LIST_INT_H
#define MAC_LIST_INT_H
#include "mac_runtime.h"
#include "mac_controls.h"
/* List Manager (Pack0, trap $A9E7), private state shared by mac_list.c (selectors and cell data) and mac_list_ui.c (geometry, scroll bars, drawing, LDEF calls, clicks).
 *
 * A ListHandle is an ordinary guest handle to an 88-byte ListRec in the public layout (rView 0, port 8, indent 12, cellSize 16, visible 20, vScroll 28, hScroll 32, selFlags 36,
 * lActive 37, listFlags 39, clikTime 40, clikLoc 44, mouseLoc 48, lClickLoc 52, lastClick 56, refCon 60, listDefProc 64, userHandle 68, dataBounds 72, cells 80, maxIndex 84). Differences,
 * stated: the cell data is owned here (a host table), and `cells` is a guest handle to a copy of it, rebuilt after every change, so a program's LDEF can read the bytes at the
 * offset it is given; `cellArray` (the offset table that follows maxIndex in a real record) does not exist, and a program that reads it directly is not supported. */
enum { LIST_REC = 88, LIST_MAX = 16, LIST_MAX_STEPS = 2048, LIST_CELL_MAX = 32767,
       R_VIEW = 0, R_PORT = 8, R_INDENT = 12, R_CSIZE = 16, R_VISIBLE = 20, R_VSCROLL = 28, R_HSCROLL = 32, R_SELFLAGS = 36, R_ACTIVE = 37, R_LISTFLAGS = 39, R_CLIKTIME = 40, R_CLIKLOC = 44,
       R_MOUSELOC = 48, R_CLICKLOC = 52, R_LASTCLICK = 56, R_REFCON = 60, R_DEFPROC = 64, R_USER = 68, R_BOUNDS = 72, R_CELLS = 80, R_MAXINDEX = 84,
       LF_VAUTO = 2, LF_HAUTO = 1, LF_DRAWOFF = 8,
       SF_ONLYONE = 0x80, SF_NODISJOINT = 0x20, SF_NOEXTEND = 0x10, SF_NORECT = 0x08, SF_USESENSE = 0x04,
       LDEF_INIT = 0, LDEF_DRAW = 1, LDEF_HILITE = 2, LDEF_CLOSE = 3 };
typedef struct { uint8_t *data; uint16_t len; uint8_t sel; } LCell;
typedef struct {
    uint32_t handle;         /* the ListHandle; 0 for a free slot */
    int top, left, rows, cols;
    LCell *cells;            /* rows * cols, row-major */
    uint32_t ldef;           /* the LDEF handle, 0 for the standard definition */
    uint32_t cells_handle;   /* the guest copy of the cell data (ListRec.cells) */
    uint32_t off_cache_valid;
} ListState;
typedef struct { int msg, sel; int16_t rect[4]; int16_t row, col; uint32_t list; } ListStep;
typedef struct MacLists {
    ListState list[LIST_MAX];
    struct { int active; ListStep *step; unsigned count, next; uint32_t sp, pop, result_at, result; unsigned result_bytes; uint32_t dispose; uint32_t saved_port; int port_changed; } job;
    uint32_t scratch;        /* guest memory for the rectangle an LDEF is given */
} MacLists;

/* mac_list.c */
int list_fail(MacRuntime *rt, const char *why);
uint32_t list_rd(MacRuntime *rt, uint32_t a, unsigned n);
void list_wr(MacRuntime *rt, uint32_t a, unsigned n, uint32_t v);
uint32_t list_rec(MacRuntime *rt, const ListState *L);                 /* the ListRec address */
ListState *list_find(MacRuntime *rt, uint32_t handle);                 /* NULL after halting for a handle that is not a live list */
LCell *list_cell(ListState *L, int row, int col);                       /* NULL outside dataBounds */
int list_rebuild(MacRuntime *rt, ListState *L);                         /* republish dataBounds, maxIndex and the guest `cells` copy; 0, or -1 after halting */
void list_dispose(MacRuntime *rt, ListState *L);                       /* frees the scroll bars, cells and handles of a list */
unsigned list_cell_offset(const ListState *L, int row, int col);
/* mac_list_ui.c */
int list_job_begin(MacRuntime *rt, uint32_t sp, uint32_t pop, uint32_t result_at, unsigned result_bytes, uint32_t result);   /* -1 halted */
int list_step(MacRuntime *rt, ListState *L, int msg, int sel, int row, int col);   /* queue a cell message (drawing is skipped when it is off or the cell is not visible) */
int list_redraw_all(MacRuntime *rt, ListState *L);                       /* queue a draw of every visible cell */
int list_job_run(MacRuntime *rt);                                       /* 1 handled (calls under way or finished), -1 halted */
int list_geometry(MacRuntime *rt, ListState *L);                        /* visible rectangle, scroll bar ranges and activation */
int list_scroll_to(MacRuntime *rt, ListState *L, int top, int left);    /* clamps, updates, queues the redraw */
int list_make_scroll_bars(MacRuntime *rt, ListState *L, int vert, int horiz);
void list_free_scroll_bars(MacRuntime *rt, ListState *L);
int list_cell_rect(MacRuntime *rt, const ListState *L, int row, int col, int16_t out[4]);   /* local rectangle of a visible cell, 0 when it is not visible */
int list_click(MacRuntime *rt, ListState *L, int v, int h, unsigned mods, int *result);     /* LClick, after the scroll bars; 0, or -1 halted */
int list_set_select(MacRuntime *rt, ListState *L, int row, int col, int on);
void list_default_sizes(MacRuntime *rt, ListState *L);
int list_autoscroll(MacRuntime *rt, ListState *L);
uint32_t list_ldef_load(MacRuntime *rt, unsigned id);
void list_set_active(MacRuntime *rt, ListState *L, int on);
#endif
