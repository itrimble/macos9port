#ifndef MAC_STDFILE_INT_H
#define MAC_STDFILE_INT_H
#include "mac_stdfile.h"
#include "mac_ditl.h"
/* Native Standard File dialog (Pack3 SFGetFile / SFPGetFile). Item numbers follow the public Standard File contract:
 * 1 Open, 2 hidden, 3 Cancel, 4 disk name, 5 Eject, 6 Drive, 7 file list, 8 its scroll bar, 9 divider, 11 prompt. */
enum { SF_MAX_FILES = 1024, SF_ROW = 16, SF_STAGE_FILTER = 1, SF_STAGE_RUN = 2,
       SF_OPEN = 1, SF_CANCEL = 3, SF_DISK = 4, SF_EJECT = 5, SF_DRIVE = 6, SF_LIST = 7, SF_SCROLL = 8, SF_PROMPT = 11 };
typedef struct MacSfState {
    int stage;
    unsigned selector;
    uint32_t trap_pc, sp, pop_bytes;      /* the trap to re-execute, its stack pointer, bytes to pop on completion */
    uint32_t reply, filter, pb;           /* guest SFReply, file-filter routine (0 = none), scratch ParamBlock */
    int16_t num_types;
    uint32_t types[4];
    int top, left, width, height;         /* dialog rectangle in global screen coordinates */
    MacDitl ditl;
    char prompt[64], volume[16];
    MacFileEntry *all;                    /* candidates after the type and visibility tests */
    unsigned all_count, next;             /* next candidate to offer to the guest filter */
    uint8_t *hide;                        /* per candidate: the guest filter asked to hide it */
    MacSfList list;
    uint8_t *backdrop;                    /* screen pixels under the dialog, restored on exit */
    int pressed, dirty, drawn;
} MacSfState;
void mac_stdfile_draw(MacRuntime *rt, const MacSfState *s);
void mac_stdfile_save(MacRuntime *rt, MacSfState *s);
void mac_stdfile_restore(MacRuntime *rt, const MacSfState *s);
/* The item under a global point, or 0. For the list, *row is the visible row; items outside the dialog are hidden. */
unsigned mac_stdfile_hit(const MacSfState *s, int x, int y, unsigned *row);
/* Scroll bar parts under a point: 1 up arrow, 2 down arrow, 3 page up, 4 page down, 0 none (thumb included). */
int mac_stdfile_scroll_zone(const MacSfState *s, int x, int y);
int mac_stdfile_item_enabled(const MacSfState *s, unsigned item);
void mac_stdfile_destroy(MacRuntime *rt);
int mac_stdfile_service(MacRuntime *rt, uint16_t opcode);
#endif
