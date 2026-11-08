#include "session_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Allocate first so an unsuccessful replacement preserves the old text. */
int window_text_set(Window *w, const char *text) {
    if (!w) return -1;
    if (!text) text = "";
    size_t n = strnlen(text, OSPI_CLIPBOARD_LIMIT + 1);
    if (n > OSPI_CLIPBOARD_LIMIT) return -1;
    char *large = NULL;
    if (n >= sizeof w->text) {
        large = malloc(n + 1); if (!large) return -1;
        memcpy(large, text, n + 1);
    } else memmove(w->text, text, n + 1);
    free(w->expanded_text); w->expanded_text = large;
    if (large) w->text[0] = 0;
    return 0;
}

void session_set_clip(Session *s, const char *text) {
    if (!s) return;
    if (!text) text = "";
    s->clip[0] = 0;
    size_t n = strnlen(text, OSPI_CLIPBOARD_LIMIT + 1);
    if (n > OSPI_CLIPBOARD_LIMIT) { s->clipboard_read_error = 1; return; }
    char *large = NULL;
    if (n >= sizeof s->clipboard_in) {
        large = malloc(n + 1);
        if (!large) { s->clipboard_read_error = 1; return; }
        memcpy(large, text, n + 1);
    } else memmove(s->clipboard_in, text, n + 1);
    snprintf(s->clip, sizeof s->clip, "%s", text);
    free(s->clipboard_large_in); s->clipboard_large_in = large;
    if (large) s->clipboard_in[0] = 0;
    s->clipboard_read_error = 0;
}
