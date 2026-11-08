#ifndef MAC_CONTROLS_H
#define MAC_CONTROLS_H
#include "mac_runtime.h"
/* Control Manager subset for guest windows: standard push button, check box, radio button and scroll bar (see mac_controls.c). The service returns
 * 1 handled, 0 not a control trap, -1 halted. */
int mac_controls_service(MacRuntime *rt, uint16_t opcode);
/* Drawing and hit testing (mac_control_draw.c) on the control record at guest address `p` of window `w`. Local coordinates are the window port's. */
void mac_control_draw(MacRuntime *rt, const MacWindowState *w, uint32_t p, int erase);
int64_t mac_control_from_cntl(MacRuntime *rt, uint32_t win, int16_t id, const uint8_t *rect);   /* CNTL -> control (see mac_controls.c) */
void mac_controls_release(MacRuntime *rt, uint32_t win);
/* Control calls whose handle is a native dialog's button, check box or radio button item (mac_dlgmgr.c): 0 not such a handle, 1 handled, -1 halted. */
int mac_dlgmgr_item_control(MacRuntime *rt, uint16_t op, uint32_t handle, uint32_t sp, unsigned args);
uint32_t mac_control_record(MacRuntime *rt, uint32_t handle);    /* the record address of a live control handle, or 0 after halting */
int mac_control_part(MacRuntime *rt, uint32_t p, int v, int h);   /* a local point -> part code, 0 for none (invisible, inactive or outside) */
/* ControlRecord offsets (from my memory of Inside Macintosh: nextControl 0, contrlOwner 4, contrlRect 8, contrlVis 16, contrlHilite 17, contrlValue 18, contrlMin
 * 20, contrlMax 22, contrlDefProc 24, contrlData 28, contrlAction 32, contrlRfCon 36, contrlTitle 40 (Str255)). */
enum { CTL_NEXT = 0, CTL_OWNER = 4, CTL_RECT = 8, CTL_VIS = 16, CTL_HILITE = 17, CTL_VALUE = 18, CTL_MIN = 20, CTL_MAX = 22, CTL_DEFPROC = 24, CTL_DATA = 28,
       CTL_ACTION = 32, CTL_REFCON = 36, CTL_TITLE = 40, CTL_BYTES = 296 };
#endif
