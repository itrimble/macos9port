/* Typing in a dialog's edit text item with a selection (mac_dlg_edit_char): a selection is replaced or deleted, with none the caret is at the end. */
#include "mac_dlg_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);fails++;}}while(0)
static int fails;
static void set(Dlg *d, const char *text, int s, int e) { memset(d, 0, sizeof *d); strcpy(d->text[0], text); d->length[0] = (unsigned)strlen(text); d->sel_start = s; d->sel_end = e; }
static int is(const Dlg *d, const char *text) { return d->length[0] == strlen(text) && !memcmp(d->text[0], text, strlen(text)); }
int main(void) {
    static Dlg d;
    set(&d, "Your Name", 0, 9); CHECK(mac_dlg_edit_char(&d, 0, 'I') == 1 && is(&d, "I") && d.sel_start == 1 && d.sel_end == 1);     /* a full selection is replaced */
    CHECK(mac_dlg_edit_char(&d, 0, 'a') == 1 && is(&d, "Ia") && d.sel_start == 2);                                              /* then it types on */
    set(&d, "Your Name", 5, 9); CHECK(mac_dlg_edit_char(&d, 0, 'X') == 1 && is(&d, "Your X"));                                    /* a partial selection */
    set(&d, "Your Name", 5, 9); CHECK(mac_dlg_edit_char(&d, 0, 8) == 1 && is(&d, "Your ") && d.sel_start == 5 && d.sel_end == 5);  /* Backspace deletes it, nothing typed */
    set(&d, "Your Name", 5, 9); CHECK(mac_dlg_edit_char(&d, 0, 0x7f) == 1 && is(&d, "Your "));                                    /* Delete too */
    set(&d, "abc", 9, 2); CHECK(mac_dlg_edit_char(&d, 0, 'Z') == 1 && is(&d, "abZ"));                                             /* reversed and past-the-end ranges are ordered and clamped */
    set(&d, "abc", -4, 1); CHECK(mac_dlg_edit_char(&d, 0, 'Z') == 1 && is(&d, "abcZ"));                                           /* a negative start is no selection: the caret is at the end */
    set(&d, "abc", 2, 2); CHECK(mac_dlg_edit_char(&d, 0, 'Z') == 1 && is(&d, "abcZ"));                                            /* an empty selection: caret at the end */
    set(&d, "abc", 0, 0); CHECK(mac_dlg_edit_char(&d, 0, 8) == 0 && is(&d, "ab"));                                                /* Backspace with no selection removes the last character */
    set(&d, "", 0, 0); CHECK(mac_dlg_edit_char(&d, 0, 8) == 0 && is(&d, ""));                                                     /* and nothing from empty text */
    set(&d, "abc", 0, 3); CHECK(mac_dlg_edit_char(&d, 0, 1) == 1 && is(&d, "abc") && d.sel_start == 0 && d.sel_end == 3);                                               /* a control character changes nothing, the selection stays for the caller */
    set(&d, "abc", 0, 3); CHECK(mac_dlg_edit_char(&d, 0, 200) == 1 && is(&d, "abc") && d.sel_end == 3);                                             /* not ASCII: ignored */
    set(&d, "", 0, 0); memset(d.text[0], 'a', TEXT_CAP); d.length[0] = TEXT_CAP;
    CHECK(mac_dlg_edit_char(&d, 0, 'b') == 0 && d.length[0] == TEXT_CAP);                                                          /* full: the character is dropped */
    d.sel_start = 0; d.sel_end = TEXT_CAP; CHECK(mac_dlg_edit_char(&d, 0, 'b') == 1 && is(&d, "b"));                              /* unless a selection makes room */
    if (!fails) puts("Dialog edit text checks passed");
    return fails ? 1 : 0;
}
