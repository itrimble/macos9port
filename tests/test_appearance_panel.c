#include "appearance_panel.h"
#include "desktop_wallpaper.h"

#include <stdio.h>
#include <stdlib.h>

static int current = -1, commands, last_command;
int desktop_wallpaper_count(void) { return 59; }
int desktop_wallpaper_current(void) { return current; }
const char *desktop_wallpaper_name(int index) { (void)index; return "Fixture Picture"; }
void session_command(Session *s, int cmd) {
    commands++;
    last_command = cmd;
    current = cmd - CMD_WALLPAPER_BASE - 1;
    s->menu = -1;
}
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    Session s = {.sw = OSPI_W, .sh = OSPI_H};
    appearance_panel_close();
    CHECK(!appearance_panel_active());
    CHECK(!appearance_panel_down(&s, 10, 40));
    appearance_panel_open_controls(&s);
    CHECK(appearance_panel_active());
    CHECK(!appearance_panel_down(&s, 30, 10)); /* Menu bar is still usable. */
    s.down = s.drag = 1;
    CHECK(appearance_panel_down(&s, 10, 40));
    CHECK(!s.down && !s.drag && commands == 0);
    CHECK(appearance_panel_down(&s, 512, 380)); /* Appearance launcher. */
    CHECK(appearance_panel_down(&s, 250, 302)); /* Picture 1. */
    CHECK(current == 0 && last_command == CMD_WALLPAPER_BASE + 1);
    CHECK(appearance_panel_down(&s, 360, 220)); /* Themes tab. */
    int before = commands;
    CHECK(appearance_panel_down(&s, 250, 302));
    CHECK(commands == before); /* No invented theme actions. */
    CHECK(appearance_panel_key(&s, '\t', 0)); /* Desktop tab. */
    for (int i = 0; i < 6; i++) CHECK(appearance_panel_down(&s, 740, 532));
    CHECK(appearance_panel_down(&s, 250, 470)); /* Last page, picture 59. */
    CHECK(current == 58 && last_command == CMD_WALLPAPER_LAST);
    before = commands;
    CHECK(appearance_panel_down(&s, 250, 491)); /* Empty final row. */
    CHECK(commands == before);
    CHECK(appearance_panel_down(&s, 250, 281));
    CHECK(current == -1 && last_command == CMD_WALLPAPER_BASE);
    CHECK(!appearance_panel_key(&s, 'q', MOD_CMD));
    CHECK(appearance_panel_active());
    CHECK(appearance_panel_key(&s, 'w', MOD_CMD));
    CHECK(!appearance_panel_active());
    appearance_panel_open(&s, APPEARANCE_THEMES_TAB);
    uint32_t *pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Fb fb;
        fb_init(&fb, pixels, OSPI_W, OSPI_H);
        appearance_panel_draw(&s, &fb);
        CHECK(pixels[(size_t)174 * OSPI_W + 212] == C_BLACK);
        CHECK(pixels[(size_t)400 * OSPI_W + 500] == C_MENU);
        CHECK(pixels[10] == 0); /* Overlay does not paint the menu bar. */
        free(pixels);
    }
    CHECK(appearance_panel_key(&s, 27, 0));
    CHECK(!appearance_panel_active());
    appearance_panel_open_controls(&s);
    CHECK(appearance_panel_key(&s, '\r', 0));
    CHECK(appearance_panel_down(&s, 225, 184)); /* Close box. */
    CHECK(!appearance_panel_active());
    printf("appearance panel: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
