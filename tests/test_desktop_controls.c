#include "ospi.h"
#include "appearance_panel.h"
#include "desktop_context.h"
#include "desktop_wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
static void click(Session *s, int x, int y) {
    session_down(s, x, y, 1);
    session_up(s, x, y);
}
int main(void) {
    char root[] = "/tmp/os999-controls-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return 1;
    Rect r;
    menu_title_rect(0, &r); click(s, r.x + 5, r.y + 5);
    CHECK(s->menu == 0);
    int controls = -1;
    for (int i = 0; i < menu_nitems(0); i++)
        if (menu_at(0, i)->cmd == CMD_CONTROL_PANELS) controls = i;
    CHECK(controls >= 0);
    menu_item_rect(0, controls, &r); click(s, r.x + 5, r.y + 5);
    CHECK(appearance_panel_active() && s->menu == -1 && !s->down);
    session_key(s, '\r', 0); /* Control Panels -> Appearance */
    CHECK(appearance_panel_active());
    int old_windows = s->nwin;
    click(s, 1000, 700); /* No click-through outside the panel. */
    CHECK(s->nwin == old_windows && appearance_panel_active());
    menu_title_rect(0, &r); click(s, r.x + 5, r.y + 5);
    CHECK(s->menu == 0); /* Menu bar remains available above panel. */
    session_key(s, 27, 0);
    CHECK(s->menu == -1 && appearance_panel_active());
    session_key(s, 'w', MOD_CMD);
    CHECK(!appearance_panel_active() && s->nwin == old_windows);
    session_context(s, 750, 600);
    CHECK(context_active() && !s->down);
    click(s, 20, 650);
    CHECK(!context_active() && s->nwin == old_windows);
    session_context(s, 750, 600);
    session_key(s, CONTEXT_KEY_DOWN, 0); /* New Folder */
    session_key(s, CONTEXT_KEY_DOWN, 0); /* Skip disabled Clean Up to Appearance. */
    session_key(s, '\r', 0);
    CHECK(!context_active() && appearance_panel_active());
    /* Plain row goes through the real persisted wallpaper command. */
    click(s, 250, 280);
    CHECK(desktop_wallpaper_current() == -1 && !s->dialog);
    char path[OSPI_PATH], value[32] = {0};
    snprintf(path, sizeof path, "%s/.os999-wallpaper", root);
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) { CHECK(fgets(value, sizeof value, f) != NULL); fclose(f); }
    CHECK(strcmp(value, "plain\n") == 0);
    session_key(s, 27, 0);
    s->dialog = 1;
    session_context(s, 700, 600);
    CHECK(!context_active() && s->dialog == 1);
    s->dialog = 0;
    session_context(s, 750, 600);
    session_destroy(s);
    CHECK(!context_active() && !appearance_panel_active());
    printf("desktop control integration: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
