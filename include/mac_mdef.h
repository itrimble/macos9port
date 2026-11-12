#ifndef MAC_MDEF_H
#define MAC_MDEF_H
#include "mac_runtime.h"
/* Guest menu definition procedures (mac_mdef.c). A menu whose record carries a menu-definition handle (menuProc, the long at offset 6 of the
 * menu record; GetMenu fills it from the MENU resource's MDEF id) runs that 'MDEF' resource's 68K code:
 *   PROCEDURE mdef(message: INTEGER; theMenu: MenuHandle; VAR menuRect: Rect; hitPt: Point; VAR whichItem: INTEGER)
 * The Menu Manager services build the call, run it with mac_guest_call (so a PowerPC caller reaches it through the 68K trap stub like any
 * other trap) and carry on from the completion. Messages sent here: draw 0, choose 1, size 2, pop-up 3. Item-level messages (4, 5) and the
 * theme-savvy handshake (7) are never sent. The procedure runs with a Window Manager style port as the current port: a screen-sized colour port once colour QuickDraw is in
 * use, else the plain Window Manager port. */
enum { MDEF_DRAW = 0, MDEF_CHOOSE = 1, MDEF_SIZE = 2, MDEF_POPUP = 3 };
typedef struct MacMdef MacMdef;
uint32_t mac_mdef_load(MacRuntime *rt, unsigned id);                    /* a locked MDEF resource handle, or 0 */
uint32_t mac_mdef_code(MacRuntime *rt, uint32_t handle);                /* the entry address of a live, locked MDEF handle, or 0 */
uint32_t mac_mdef_of_menu(MacRuntime *rt, const MacHandle *menu);       /* the live MDEF handle in a menu record, or 0 for a standard menu */
/* Sends one message. `rect` (top, left, bottom, right) is menuRect on entry, `hit_v`/`hit_h` hitPt, `item` whichItem. 0 with the call under
 * way (the service returns handled), -1 halted. `done(rt, ctx, 0)` runs when the procedure returns; read the outputs with mac_mdef_out_*. */
int mac_mdef_send(MacRuntime *rt, uint32_t menu, int message, const int16_t rect[4], int hit_v, int hit_h, int item, MacGuestDone done, void *ctx);
void mac_mdef_out(MacRuntime *rt, int16_t rect[4], int *item);            /* menuRect and whichItem after the call */
/* Sizes each menu with the size message in turn; when the last returns, stores `result` (4 bytes) at `result_at` if non-zero and pops `pop`
 * bytes of the trap's arguments from `sp`. 1 handled (the first call under way), -1 halted. */
int mac_mdef_size_menus(MacRuntime *rt, const uint32_t *menus, unsigned count, uint32_t sp, unsigned pop, uint32_t result_at, uint32_t result);
void mac_mdef_destroy(MacRuntime *rt);
#endif
