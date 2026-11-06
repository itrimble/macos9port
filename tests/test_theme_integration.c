#include "ospi.h"
#include "draw.h"
#include "desktop_theme.h"
#include "desktop_wallpaper.h"
#include "appearance_panel.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)
int main(void) {
    char root[] = "/tmp/os999-theme-integration-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    Session *s = session_create(root);
    uint32_t *px = calloc(OSPI_W * OSPI_H, sizeof *px);
    if (!s || !px) return 1;
    CHECK(desktop_theme_current() == 0);
    Window *w = &s->win[0];
    CHECK(w->nitems > 0);
    w->items[0].kind = ITEM_APPLICATION;
    w->items[0].has_guest_icon = 1;
    memset(w->items[0].guest_icon, 215, sizeof w->items[0].guest_icon);
    memset(w->items[0].guest_icon_mask, 255, sizeof w->items[0].guest_icon_mask);
    Layout lay;
    window_layout(w, &lay);
    int ix = lay.content.x + (CELL_W - 32) / 2;
    int iy = lay.content.y + 4;
    int wallpaper = desktop_wallpaper_current();
    for (int theme = 0; theme < 16; theme++) {
        session_command(s, CMD_THEME_BASE + theme);
        CHECK(desktop_theme_current() == theme && !s->dialog);
        session_paint(s, px, OSPI_W, OSPI_H);
        CHECK(px[OSPI_W + 800] == desktop_theme_color(THEME_MENU));
        CHECK(px[(lay.content.y + lay.content.h - 8) * OSPI_W + lay.content.x + 20] == desktop_theme_color(THEME_PANEL));
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++)
            CHECK(px[(iy + y) * OSPI_W + ix + x] == 0xff000000u);
        CHECK(desktop_wallpaper_current() == wallpaper);
    }
    session_key(s, 't', MOD_CMD);
    CHECK(desktop_theme_current() == 0);
    appearance_panel_open(s, APPEARANCE_THEMES_TAB);
    session_key(s, 't', MOD_CMD);
    CHECK(desktop_theme_current() == 1);
    session_key(s, 27, 0);
    session_command(s, 217); /* Reserved/invalid theme command is inert. */
    CHECK(desktop_theme_current() == 1);
    session_destroy(s);
    s = session_create(root);
    CHECK(s && desktop_theme_current() == 1);
    session_command(s, CMD_THEME_BASE);
    CHECK(desktop_theme_color(THEME_MENU) == C_MENU);
    CHECK(desktop_theme_color(THEME_PANEL) == C_WHITE);
    session_destroy(s);
    free(px);
    printf("production theme integration: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
