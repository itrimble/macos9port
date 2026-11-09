#ifndef MAC_CDEF_H
#define MAC_CDEF_H
#include "mac_controls.h"
/* Guest control definition procedures (mac_cdef.c). A control whose procID is id*16+variant with id >= 2 runs the program's own 'CDEF' resource
 * id: FUNCTION cdef(varCode: INTEGER; theControl: ControlHandle; message: INTEGER; param: LONGINT): LONGINT. The Control Manager services below
 * build a job (a list of messages to send, in order), start the first guest call and return; each completion starts the next, and the last one
 * runs the job's finish callback and pops the trap's arguments. Messages (from memory of Inside Macintosh): drawCntl 0, testCntl 1, initCntl 3,
 * dispCntl 4. One job at a time: Control Manager traps made by a running CDEF halt. */
enum { CDEF_DRAW = 0, CDEF_TEST = 1, CDEF_INIT = 3, CDEF_DISP = 4, CDEF_AUTOTRACK = 8, CDEF_MAX_STEPS = 520 };
typedef struct MacCdefJob MacCdefJob;
typedef int (*MacCdefFinish)(MacRuntime *rt, MacCdefJob *job);
struct MacCdefJob {
    int active;
    struct { uint32_t handle; int message; uint32_t param; } step[CDEF_MAX_STEPS];
    unsigned count, next;
    uint32_t sp, args;                       /* the trap's stack pointer and argument bytes to pop */
    uint32_t saved_port, port_changed;
    MacCdefFinish finish;
    uint32_t user[4];                        /* finish's private values */
    int cur_message, part, keep_going; uint32_t found;   /* keep_going: a testCntl that finds a part does not end the job (TrackControl) */
                   /* result of a testCntl step that answered non-zero */
};
int mac_cdef_finish_result(MacRuntime *rt, MacCdefJob *job);   /* a finish that writes user[2] bytes of user[1] at user[0] */
int mac_cdef_custom_id(unsigned proc);       /* the CDEF id for a procID, or 0 for the standard definitions */
uint32_t mac_cdef_load(MacRuntime *rt, unsigned id);   /* a locked resource handle for CDEF id, or 0 */
int mac_cdef_job_new(MacRuntime *rt, uint32_t sp, uint32_t args, MacCdefFinish finish);   /* -1 (halted) on failure */
int mac_cdef_add(MacRuntime *rt, uint32_t handle, int message, uint32_t param);          /* -1 (halted) when full */
int mac_cdef_start(MacRuntime *rt);          /* runs the job: 1 handled (a guest call is under way or the job finished), -1 halted */
void mac_cdef_drop(MacRuntime *rt, int message);   /* removes the queued steps with this message */
int mac_cdef_busy(const MacRuntime *rt);
#endif
