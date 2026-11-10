/* Command/menu integration fixture. Bitmap decoding and persistence are tested
 * separately by test_desktop_wallpaper; this loader makes all 59 entries cheap. */
#include "ospi.h"
#include "desktop_wallpaper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int current = -1, calls, save_failure;
static char preference[OSPI_PATH + 32];
int desktop_wallpaper_init(const char *directory, const char *path) {
    (void)directory;
    snprintf(preference, sizeof preference, "%s", path);
    current = -1;
    return 1;
}
int desktop_wallpaper_count(void) { return 59; }
const char *desktop_wallpaper_name(int index) { (void)index; return "Fixture Wallpaper"; }
int desktop_wallpaper_current(void) { return current; }
int desktop_wallpaper_select(int index) {
    calls++;
    if (save_failure || index < -1 || index >= 59) return -1;
    current = index;
    return index < 0 ? 0 : 1;
}
int desktop_wallpaper_draw(Fb *fb) {
    if (current < 0) return 0;
    fb_fill(fb, 0, 0, fb->w, fb->h, 0xFFFFFDF0u);
    return 1;
}
void desktop_wallpaper_shutdown(void) { current = -1; }
static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static int item_for(int command) {
    for (int i = 0; i < menu_nitems(MENU_APPEARANCE); i++)
        if (menu_at(MENU_APPEARANCE, i)->cmd == command) return i;
    return -1;
}
static void click_command(Session *s, int command) {
    int item = item_for(command);
    CHECK(item >= 0);
    if (item < 0) return;
    Rect r;
    menu_item_rect(MENU_APPEARANCE, item, &r);
    s->menu = MENU_APPEARANCE;
    session_down(s, r.x + 10, r.y + r.h / 2, 1);
}

int main(void) {
    char root[] = "/tmp/os999-appearance-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return 1;
    CHECK(strstr(preference, "/.os999-wallpaper") != NULL);
    CHECK(menu_count() == 9 && !strcmp(menu_title(5), "Help") && !strcmp(menu_title(8), "Window"));
    CHECK(!strcmp(menu_title(6), "Appearance"));
    CHECK(menu_cmd_for_key('o') == CMD_OPEN);
    CHECK(menu_cmd_for_key('t') == CMD_THEME_NEXT);
    CHECK(menu_at(MENU_APPEARANCE, item_for(CMD_WALLPAPER_BASE))->flags & MI_CHECKED);
    CHECK(menu_nitems(MENU_APPEARANCE) == 18);
    CHECK(menu_at(MENU_APPEARANCE, item_for(CMD_WALLPAPER_PREVIOUS))->flags & MI_DISABLED);
    for (int page = 1; page <= 5; page++) {
        click_command(s, CMD_WALLPAPER_NEXT);
        CHECK(s->menu == MENU_APPEARANCE && menu_wallpaper_current_page() == page);
    }
    CHECK(menu_nitems(MENU_APPEARANCE) == 17);
    CHECK(menu_at(MENU_APPEARANCE, item_for(CMD_WALLPAPER_NEXT))->flags & MI_DISABLED);
    click_command(s, CMD_WALLPAPER_NEXT);
    CHECK(menu_wallpaper_current_page() == 5);
    click_command(s, CMD_WALLPAPER_LAST);
    CHECK(current == 58 && s->menu == -1);
    CHECK(menu_at(MENU_APPEARANCE, item_for(CMD_WALLPAPER_LAST))->flags & MI_CHECKED);
    int before = calls;
    session_command(s, CMD_WALLPAPER_LAST + 1);
    session_command(s, CMD_THEME_BASE);
    CHECK(calls == before && current == 58);
    click_command(s, CMD_WALLPAPER_BASE);
    CHECK(current == -1 && menu_at(MENU_APPEARANCE, item_for(CMD_WALLPAPER_BASE))->flags & MI_CHECKED);
    save_failure = 1;
    session_command(s, CMD_WALLPAPER_LAST);
    CHECK(s->dialog && current == -1);
    /* On a bright picture the unselected label has an opaque dark plate.
       Plain gradient styling and selected-label black remain unchanged. */
    s->nwin = s->dialog = 0;
    s->menu = s->desk_sel = -1;
    uint32_t *pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Rect icon, hit;
        desktop_icon_rect(OSPI_W, OSPI_H, 0, &icon, &hit);
        int x = icon.x + 16 - fb_text_width("OS999 HD") / 2 - 2;
        int y = icon.y + 33;
        size_t at = (size_t)y * OSPI_W + (size_t)x;
        current = -1;
        session_paint(s, pixels, OSPI_W, OSPI_H);
        uint32_t plain = pixels[at];
        CHECK(plain != 0xFF26304Au);
        current = 0;
        session_paint(s, pixels, OSPI_W, OSPI_H);
        CHECK(pixels[at] == 0xFF26304Au);
        CHECK(pixels[(size_t)(OSPI_H / 2) * OSPI_W + OSPI_W / 2] == 0xFFFFFDF0u);
        s->desk_sel = 0;
        session_paint(s, pixels, OSPI_W, OSPI_H);
        CHECK(pixels[at] == C_BLACK);
        s->desk_sel = -1;
        current = -1;
        session_paint(s, pixels, OSPI_W, OSPI_H);
        CHECK(pixels[at] == plain);
        free(pixels);
    }
    session_destroy(s);
    char theme_path[OSPI_PATH];
    snprintf(theme_path, sizeof theme_path, "%s/.os999-theme", root);
    CHECK(unlink(theme_path) == 0);
    snprintf(theme_path, sizeof theme_path, "%s/.os999-finder-state", root);
    CHECK(unlink(theme_path) == 0);
    snprintf(theme_path, sizeof theme_path, "%s/Desktop Folder", root);
    CHECK(rmdir(theme_path) == 0);
    CHECK(rmdir(root) == 0);
    printf("appearance: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
