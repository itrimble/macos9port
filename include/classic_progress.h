#ifndef OS999_CLASSIC_PROGRESS_H
#define OS999_CLASSIC_PROGRESS_H
/* Progress model for the Install Classic Apps dialog: per-item stage and byte counts, a byte-weighted
 * total, the labels the dialog shows, a text wrapper and the classic bordered bar. Pure maths and
 * drawing; no threads (the dialog guards a ClassicRunProg with its own mutex). */
#include "classic_apps.h"
#include "draw.h"

/* One item's progress runs 0..1000 (per mille): download/copy 0..800, verify 800..850, unpack 850..1000.
 * Verifying and unpacking are therefore a fixed share of every item, whatever its size. */
enum { CP_DL_END = 800, CP_VERIFY_END = 850, CP_FULL = 1000, CP_DEFAULT_WEIGHT = 1048576 };

typedef struct {
    ClassicStage stage;
    uint64_t got, total;       /* bytes of the current phase; total 0 = unknown */
    uint64_t weight;           /* bytes this item counts for in the total (> 0) */
    char reason[64];           /* short reason when FAILED */
} ClassicItemProg;

typedef struct ClassicRunProg { ClassicItemProg it[CLASSIC_MAX_APPS + 1]; int n; } ClassicRunProg;

uint64_t classic_item_weight(const ClassicApp *app);                      /* size, else size_mb, else 1 MB */
void classic_prog_init(ClassicRunProg *p);
int classic_prog_add(ClassicRunProg *p, uint64_t weight);                 /* returns the slot, or -1 when full */
/* Move an item to a stage. Forward only; DONE, FAILED and STOPPED are final. Resets the byte counts. */
void classic_prog_stage(ClassicRunProg *p, int i, ClassicStage st);
void classic_prog_bytes(ClassicRunProg *p, int i, uint64_t got, uint64_t total);
void classic_prog_fail(ClassicRunProg *p, int i, const char *err);
void classic_prog_stop_rest(ClassicRunProg *p);                           /* every item not yet final becomes STOPPED */

int classic_item_permille(const ClassicItemProg *it);                      /* 0..1000, as reached */
int classic_total_permille(const ClassicRunProg *p);                       /* weighted; DONE/FAILED count full */
int classic_prog_finished(const ClassicRunProg *p);                        /* items in a final stage */
int classic_prog_all_final(const ClassicRunProg *p);

void classic_stage_label(const ClassicItemProg *it, char *out, size_t n);   /* Waiting / Downloading 3.1 of 8.3 MB / ... */
void classic_total_label(const ClassicRunProg *p, char *out, size_t n);     /* Installing 2 of 3, 47% */
void classic_summary(const ClassicRunProg *p, char *out, size_t n);         /* one line once everything is final */
void classic_short_reason(const char *err, char *out, size_t n);            /* first sentence, ellipsized to fit n */

/* Word wrap by measured pixel width. Returns lines used (<= max_lines); if text remains the last line ends in "...". */
typedef int (*ClassicMeasure)(const char *s, int nbytes);
typedef struct { char t[128]; } ClassicLine;
int classic_wrap(const char *s, int max_w, ClassicMeasure measure, ClassicLine *out, int max_lines);

/* Bordered bar: 1 px black frame, white inside, black fill for permille/1000 of the inside width. */
void classic_bar_paint(Fb *fb, int x, int y, int w, int h, int permille);
int classic_bar_fill_width(int w, int permille);                            /* inside pixels filled */
#endif
