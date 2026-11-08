#include "session_int.h"
#include "finder_apps.h"
#include "archive_fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int checks, failures, media_running, activations, guest_calls, guest_result = 1;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static int running(const Session *s) { (void)s; return media_running; }
static void activate(Session *s) { (void)s; activations++; }
static int guest_command(int command) { CHECK(command == CMD_CLOSE); guest_calls++; return guest_result; }
static int active(const Session *s) {
    CHECK(s->guest_focused + s->media_focused + s->browser_focused <= 1);
    return s->guest_focused ? 2 : s->media_focused ? 3 : s->browser_focused ? 4 : 1;
}
static void cycle(Session *s, int reverse, int expected) {
    CHECK(finder_apps_key(s, '\t', MOD_CMD | (reverse ? MOD_SHIFT : 0), 1, 0));
    CHECK(active(s) == expected);
    CHECK(finder_apps_key(s, '\t', 0, 0, 0));
}
int main(void) {
    char root[] = "/tmp/os999-app-switch-XXXXXX";
    CHECK(mkdtemp(root) != NULL); CHECK(volume_seed(root) == 0);
    SessionMediaHooks hooks = {.running=running, .activate=activate};
    session_set_media_hooks(&hooks);
    Session *s = session_create(root); CHECK(s != NULL); if (!s) return 1;
    cycle(s, 0, 1);
    s->guest_active = s->browser_active = media_running = 1;
    s->guest_menu_command = guest_command;
    cycle(s, 0, 2); cycle(s, 0, 3); cycle(s, 0, 4); cycle(s, 0, 1);
    cycle(s, 1, 4); cycle(s, 1, 3); cycle(s, 1, 2); cycle(s, 1, 1);
    s->media_hidden = 1; cycle(s, 0, 2); cycle(s, 0, 3);
    CHECK(!s->media_hidden && activations >= 3);
    CHECK(finder_apps_key(s, '\t', MOD_CMD, 1, 0)); CHECK(active(s) == 4);
    CHECK(finder_apps_key(s, '\t', MOD_CMD, 1, 1)); CHECK(active(s) == 4);
    s->dialog = 1; CHECK(finder_apps_key(s, '\t', 0, 0, 0));
    CHECK(!finder_apps_key(s, '\t', MOD_CMD, 1, 0) && active(s) == 4);
    s->dialog = 0; s->menu = 1;
    CHECK(!finder_apps_key(s, '\t', MOD_CMD, 1, 0)); s->menu = -1;
    CHECK(!finder_apps_key(s, '\t', 0, 1, 0));
    CHECK(!finder_apps_key(s, '\t', 0, 0, 0));
    CHECK(!finder_apps_key(s, '\t', MOD_CMD | MOD_OTHER, 1, 0));
    CHECK(!finder_apps_key(s, 'x', MOD_CMD, 1, 0));
    media_running = 0; s->guest_active = 0;
    cycle(s, 0, 1); cycle(s, 0, 4); cycle(s, 1, 1);
    /* The host guest close box requests guest Close; it never closes Finder. */
    s->guest_active = 1; int windows = s->nwin;
    finder_apps_close_guest_window(s);
    CHECK(guest_calls == 1 && active(s) == 2 && s->nwin == windows && !s->quit);
    guest_result = 0; finder_apps_close_guest_window(s);
    CHECK(guest_calls == 2 && s->dialog && !s->quit && s->guest_active);
    finder_apps_close_guest_window(s); CHECK(guest_calls == 2);
    s->dialog = 0; s->guest_active = 0;
    finder_apps_close_guest_window(s); CHECK(guest_calls == 2);
    session_destroy(s); session_set_media_hooks(NULL);
    CHECK(archive_remove_tree(root) == 0);
    printf("Application switching: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
