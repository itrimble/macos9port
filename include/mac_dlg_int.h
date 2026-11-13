#ifndef MAC_DLG_INT_H
#define MAC_DLG_INT_H
#include "mac_runtime.h"
#include "mac_dialog_draw.h"
#include "mac_ditl.h"
/* Internals shared by the Dialog Manager files (mac_dlgmgr.c, mac_dlgdraw.c, mac_dlgwindow.c). */
enum { DLOG_TYPE = 0x444c4f47, DITL_TYPE = 0x4449544c, ICON_TYPE = 0x49434f4e, MAX_OPEN = 4, TEXT_CAP = 255, RECORD = 170, FILTER_KEY = 0xbf1, FILTER_OP = 0xabf1 };
#define STD_FILTER mac_native_stub(FILTER_KEY)   /* a guest-callable stub: the standard ModalFilterProc */
typedef struct {
    uint32_t ptr; int16_t id; int left, top, width, height; int visible_flag;
    MacDitl ditl; uint8_t length[MAC_DITL_MAX_ITEMS]; char text[MAC_DITL_MAX_ITEMS][TEXT_CAP + 1]; int16_t value[MAC_DITL_MAX_ITEMS];
    uint32_t handle[MAC_DITL_MAX_ITEMS], block[MAC_DITL_MAX_ITEMS];   /* item handles and the text blocks they point at: the host's own record of both */
    uint32_t region[5];                                            /* strucRgn, contRgn, updateRgn, and the GrafPort's visRgn and clipRgn handles, owned as ordinary regions */
    uint8_t item_hidden[MAC_DITL_MAX_ITEMS];                       /* HideDItem: not drawn, not hit, and its box reports 16384 pixels to the right */
    int edit_item, sel_start, sel_end, def_item, cancel_item, hidden;   /* hidden: drawn nothing until ShowWindow */
    uint8_t *backdrop; int drawn;
    int modal, pressed; uint32_t sp, pc, hit_ptr;
    uint32_t filter, scratch; MacEvent pend; int pend_real;     /* ModalDialog filterProc: the guest routine, its scratch (event + itemHit) and the event it is looking at */
    uint32_t port_window; int ctl_pending; unsigned last_hash;   /* last_hash: the state the native drawing last showed */
                         /* a hidden registered window that is the dialog's colour port (control items), and a redraw of its controls owed */
    int update_due;                                              /* the native drawing repainted the dialog: a ModalDialog filter procedure is owed an update event, so a program that draws its own items repaints them */
    uint32_t user_proc[MAC_DITL_MAX_ITEMS];                      /* SetDItem: the procedure that draws a user item (mac_dlguser.c) */
    int user_due, user_running, user_next; uint32_t user_sp, user_pc, user_prev_port, user_prev_current, user_prev_dlgport;
    int select_busy; uint32_t select_dlg_ptr;                    /* DialogSelect waiting for the mouse-up of a press */
} Dlg;
typedef struct MacDlgMgr { Dlg *open[MAX_OPEN]; uint32_t port; /* the dialog SetPort last named, 0 once any other port is set */ } Mgr;
#define M(rt) ((Mgr *)(rt)->dlgmgr)


int mac_dlg_set_title(MacRuntime *rt, Dlg *d, uint32_t src);                           /* SetWTitle on a dialog (mac_dlgtitle.c) */
int mac_dlg_set_item(MacRuntime *rt, Dlg *d, uint32_t sp);                            /* SetDItem (mac_dlguser.c) */
int mac_dlg_user_start(MacRuntime *rt, Dlg *d, uint32_t sp, uint32_t pc);             /* call the owed user item procedures: 0 none, 1 under way, -1 halted */
int mac_dlg_port_regions(MacRuntime *rt, Dlg *d);   /* the DialogPtr's GrafPort visRgn (+24, the dialog's rectangle in local coordinates) and clipRgn (+28, wide open): 1, 0 if out of memory (mac_dlgdraw.c) */
int mac_dlg_edit_char(Dlg *d, unsigned k, unsigned ch);   /* typing / Backspace in an edit item, with SelIText selections (mac_dlgedit.c) */
void mac_dlg_draw_pict(MacRuntime *rt, int l, int t, int r, int b, int16_t id);   /* a PICT item, scaled to its rectangle (mac_dlgpict.c) */
static inline int has_text(const MacDitlItem *it) { return it->kind == MAC_ITEM_STATIC || it->kind == MAC_ITEM_EDIT || it->kind == MAC_ITEM_BUTTON || it->kind == MAC_ITEM_CHECK || it->kind == MAC_ITEM_RADIO; }
uint32_t mac_dlg_new_rect_region(MacRuntime *rt, int t, int l, int b, int r);
void mac_dlg_set_region(MacRuntime *rt, uint32_t h, int t, int l, int b, int r);
void mac_dlg_free_region(MacRuntime *rt, uint32_t h);
unsigned mac_dlg_expand(const MacRuntime *rt, const Dlg *d, unsigned k, char *out, unsigned cap);
void mac_dlg_draw(MacRuntime *rt, Dlg *d);
unsigned mac_dlg_state_hash(const MacRuntime *rt, const Dlg *d);   /* everything the native drawing depends on */
#endif
