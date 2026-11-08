#include "session_int.h"
#include "finder_search.h"
#include "desktop_commands.h"
#include "finder_print.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static void fixture(const char *root, const char *prefix, int number) {
    char path[1024];
    snprintf(path, sizeof path, "%s/Documents/%s %03d - a long filename used to verify complete clipboard contents", root, prefix, number);
    FILE *f = fopen(path, "w"); CHECK(f != NULL); if (f) fclose(f);
}
int main(void) {
    char root[] = "/tmp/os999-search-copy-XXXXXX", moved[1024];
    CHECK(mkdtemp(root) != NULL); CHECK(volume_seed(root) == 0);
    for (int i = 0; i < 137; i++) fixture(root, "Needle", i);
    for (int i = 0; i < 48; i++) fixture(root, "Exact", i);
    Item bounded[48]; int limited = 0;
    CHECK(finder_search(root, "Exact", bounded, 48, &limited) == 48 && !limited);
    CHECK(finder_search(root, "Needle", bounded, 48, &limited) == 48 && limited);
    Session *s = session_create(root); CHECK(s != NULL); if (!s) return 1;
    strcpy(s->find_query, "Needle"); CHECK(finder_search_command(s, CMD_FIND_AGAIN));
    Window *w = &s->win[s->nwin - 1];
    CHECK(w->search_results && w->nitems == 137 && !w->listing_limited && !s->dialog);
    CHECK(strstr(window_items(w)[136].location, "Needle 136") != NULL);
    session_command(s, CMD_SELECT_ALL); session_command(s, CMD_COPY);
    const char *copied = session_clipboard_output(s);
    CHECK(s->clipboard_write_request && strlen(copied) > OSPI_TEXT && !s->dialog);
    char *expected = strdup(copied); CHECK(expected != NULL);
    int lines = 1; for (const char *p = copied; *p; p++) lines += *p == '\n';
    CHECK(lines == 137 && strstr(copied, "Needle 136") != NULL);
    session_command(s, CMD_SHOW_CLIPBOARD); w = &s->win[s->nwin - 1];
    CHECK(w->kind == WIN_TEXT && !strcmp(window_text(w), expected));
    CHECK(window_text_set(w, window_text(w)) == 0 && !strcmp(window_text(w), expected));
    session_command(s, CMD_COPY); CHECK(!strcmp(session_clipboard_output(s), expected));
    session_set_clip(s, expected); s->clipboard_write_request = 0;
    session_command(s, CMD_SHOW_CLIPBOARD);
    CHECK(!strcmp(window_text(&s->win[s->nwin - 1]), expected));
    session_set_clip(s, "small"); session_command(s, CMD_SHOW_CLIPBOARD);
    CHECK(!strcmp(window_text(&s->win[s->nwin - 1]), "small"));
    /* Failed repeat searches retain the previous result set. */
    snprintf(moved, sizeof moved, "%s-away", root); CHECK(rename(root, moved) == 0);
    CHECK(finder_search_command(s, CMD_FIND_AGAIN)); CHECK(s->dialog);
    int retained = 0;
    for (int i = 0; i < s->nwin; i++) if (s->win[i].search_results) retained = s->win[i].nitems;
    CHECK(retained == 137); CHECK(rename(moved, root) == 0);
    s->dialog = 0;
    char *large = malloc(OSPI_CLIPBOARD_LIMIT + 2); CHECK(large != NULL);
    if (large) {
        memset(large, 'x', OSPI_CLIPBOARD_LIMIT); large[OSPI_CLIPBOARD_LIMIT] = 0;
        session_set_clip(s, large);
        CHECK(!s->clipboard_read_error && strlen(session_clipboard_input(s)) == OSPI_CLIPBOARD_LIMIT);
        large[OSPI_CLIPBOARD_LIMIT] = 'x'; large[OSPI_CLIPBOARD_LIMIT + 1] = 0;
        session_set_clip(s, large); CHECK(s->clipboard_read_error);
        int windows = s->nwin; session_command(s, CMD_SHOW_CLIPBOARD);
        CHECK(s->dialog && s->nwin == windows);
        s->dialog = 0; session_set_clip(s, "recovered"); session_command(s, CMD_SHOW_CLIPBOARD);
        w = &s->win[s->nwin - 1]; CHECK(!strcmp(window_text(w), "recovered"));
        large[65536] = 0; CHECK(window_text_set(w, large) == 0);
        s->print_available = 1; strcpy(s->print_text, "preserved");
        CHECK(finder_print_command(s, CMD_PRINT));
        CHECK(s->dialog && !s->print_request && !strcmp(s->print_text, "preserved"));
        free(large);
    }
    free(expected); session_destroy(s); CHECK(archive_remove_tree(root) == 0);
    printf("Search and clipboard capacity: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
