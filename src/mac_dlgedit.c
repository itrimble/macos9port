#include "mac_dlg_int.h"
#include <string.h>

/* One typed character (or Backspace / Delete) in the edit item k of a dialog. A selection (set by SelIText) is replaced by what is typed, or deleted by Backspace; with no
 * selection the caret is at the end of the text. Returns 1 when the selection/caret was settled here (the caller then returns), 0 when the caller should place the caret
 * at the end of the text (Backspace with no selection, or a full field). */
int mac_dlg_edit_char(Dlg *d, unsigned k, unsigned ch) {
    int a = d->sel_start, b = d->sel_end;
    if (b < a) { int x = a; a = b; b = x; }
    if (b > (int)d->length[k]) b = (int)d->length[k];
    if (a < 0 || a >= b) a = b = (int)d->length[k];
    int typing = ch >= 32 && ch < 127, erasing = ch == 8 || ch == 0x7f;
    if (!typing && !erasing) return 1;                                      /* any other key leaves the text and the selection alone */
    if ((typing || erasing) && a < b) {
        memmove(d->text[k] + a, d->text[k] + b, d->length[k] - (unsigned)b);
        d->length[k] -= (unsigned)(b - a);
        if (erasing) { typing = 0; erasing = 0; }
    } else if (erasing && d->length[k]) d->length[k]--;
    if (typing && d->length[k] < TEXT_CAP) {
        memmove(d->text[k] + a + 1, d->text[k] + a, d->length[k] - (unsigned)a);
        d->text[k][a] = (char)ch; d->length[k]++;
        d->sel_start = d->sel_end = a + 1;
        return 1;
    }
    if (a < b) { d->sel_start = d->sel_end = a; return 1; }
    return 0;
}
