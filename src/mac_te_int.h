#ifndef MAC_TE_INT_H
#define MAC_TE_INT_H
#include "mac_text.h"
/* Multistyled TextEdit (mac_testyle.c) and its seams with the TextEdit service (mac_textedit.c). A multistyled edit record is a TERec whose txSize is -1 and whose txFont and txFace
 * fields together hold the TEStyleHandle; lineHeight and fontAscent are -1 and each line's height and ascent come from the line-height table. The style record, the style table
 * (STElement, 18 bytes each), the style runs (startChar, styleIndex), the line-height table (LHElement) and the null style are guest handles in the public layouts, so a program that
 * reads them sees real data. Limits: 2000 runs and 512 distinct styles per record; a face beyond bold and underline is drawn plain (no italic, outline, shadow, condense, extend). */
enum { TE_FONT = 74, TE_SIZE = 80, TE_LINE_H = 24, TE_ASC = 26, TE_SELS = 32, TE_SELE = 34, TE_LEN = 60, TE_DEST = 0 };
typedef struct { int end; MacTextFace f; int ascent, height; uint16_t rgb[3]; } TeSeg;
int mac_testyle_is(MacRuntime *rt, uint32_t rec);                                       /* txSize == -1 */
/* The style of the characters starting at `off`, ending at the next style boundary or `limit` (whichever is first). 0, or -1 after halting. */
int mac_testyle_seg(MacRuntime *rt, uint32_t rec, int off, int limit, TeSeg *out);
/* Writes the line-height table for the given line starts (nLines entries, then the text length) and returns 0, or -1 after halting. */
int mac_testyle_set_lines(MacRuntime *rt, uint32_t rec, const uint16_t *starts, int lines, int len);
int mac_testyle_line_h(MacRuntime *rt, uint32_t rec, int k, int *height, int *ascent);   /* an entry of the line-height table */
int mac_testyle_new(MacRuntime *rt, uint32_t hte, uint32_t rec);                         /* allocates the style records for a record TENew-style filled in by TEStyleNew */
void mac_testyle_dispose(MacRuntime *rt, uint32_t rec);                                  /* releases them */
/* The text changed: `removed` bytes at `at` were replaced by `added`; the runs follow (the new bytes take the null style, else the style before them). */
int mac_testyle_edit(MacRuntime *rt, uint32_t rec, int at, int removed, int added);
void mac_testyle_clear_null(MacRuntime *rt, uint32_t rec);                               /* the selection moved: the null scrap is emptied */
unsigned mac_testyle_fg(MacRuntime *rt, const uint16_t rgb[3]);                          /* nearest palette index of an RGBColor */
/* TEDispatch selectors on multistyled records (mac_testyle.c): 1 handled, 0 not one of these, -1 halted. */
int mac_testyle_dispatch(MacRuntime *rt, unsigned sel, uint32_t sp);
/* Seams in mac_textedit.c */
int mac_te_check(MacRuntime *rt, uint32_t h, uint32_t *rec);        /* validates a TEHandle; 0, or -1 after halting */
uint32_t mac_te_rec(MacRuntime *rt, uint32_t h);
int mac_te_recalc(MacRuntime *rt, uint32_t h);
int mac_te_after_edit(MacRuntime *rt, uint32_t h, int at, int erase);   /* recalculates the lines and repaints from the line before `at` to the bottom of the view */
int mac_te_redraw_all(MacRuntime *rt, uint32_t h);                  /* erases and repaints the whole view */
int mac_te_set_size(MacRuntime *rt, uint32_t handle, uint32_t size);
int mac_te_insert(MacRuntime *rt, uint32_t h, int at, uint32_t text, int n);   /* inserts n bytes of guest text at `at` (no repaint), the style runs follow; 0, or -1 after halting */
int mac_te_marks(MacRuntime *rt, uint32_t rec, int show);          /* remove (0) or restore (1) the selection mark before and after a change */
#endif
