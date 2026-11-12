#include "appearance_panel.h"
#include "desktop_wallpaper.h"
#include "desktop_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "desktop_assets.h"

static int occluded;
/* This isolated panel fixture does not test label persistence or editing. */
const char *finder_label_name(int label) { (void)label; return "Fixture Label"; }
int finder_apps_occluded(const Session *s, int app, int x, int y) { (void)s; (void)app; (void)x; (void)y; return occluded; }
static int current = -1, commands, last_command, icon_draws, selected_theme;
int desktop_theme_count(void) { return 16; }
int desktop_theme_current(void) { return selected_theme; }
const char *desktop_theme_name(int index) { return index ? "Fixture Color Theme" : "Platinum"; }
uint32_t desktop_theme_color(int role) {
    static const uint32_t platinum[] = {C_WHITE, C_MENU, C_BLACK, C_DISABLED, C_BLACK,
        C_TRACK, C_STRIPE_A, C_STRIPE_B, 0xFF9A98EEu, C_DESK, C_DESK2};
    if (selected_theme == 2) {
        if (role == THEME_PANEL) return 0xFF20252Du;
        if (role == THEME_MENU) return 0xFF303640u;
        if (role == THEME_FOREGROUND || role == THEME_FRAME) return C_WHITE;
        if (role == THEME_ACCENT) return 0xFF6F86ACu;
    }
    return role >= 0 && role < THEME_ROLE_COUNT ? platinum[role] : C_BLACK;
}
int desktop_asset_draw(Fb *fb, int x, int y, const char *name) {
    if (strcmp(name, "control_panel")) return 0;
    icon_draws++;
    fb_fill(fb, x, y, 32, 32, 0xFF123456u);
    return 1;
}
int desktop_wallpaper_count(void) { return 59; }
int desktop_wallpaper_current(void) { return current; }
const char *desktop_wallpaper_name(int index) { (void)index; return "Fixture Picture"; }
void session_command(Session *s, int cmd) {
    commands++;
    last_command = cmd;
    if (cmd == CMD_APP_FINDER) s->browser_focused = s->guest_focused = s->media_focused = 0;
    else if (cmd >= CMD_THEME_BASE && cmd <= CMD_THEME_LAST) selected_theme = cmd - CMD_THEME_BASE;
    else current = cmd - CMD_WALLPAPER_BASE - 1;
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
    CHECK(!appearance_panel_down(&s, 10, 40)); /* Outside clicks pass to the exposed app. */
    CHECK(s.down && s.drag && commands == 0);
    CHECK(appearance_panel_down(&s, 512, 380)); /* Appearance launcher. */
    CHECK(appearance_panel_down(&s, 250, 302)); /* Picture 1. */
    CHECK(current == 0 && last_command == CMD_WALLPAPER_BASE + 1);
    CHECK(appearance_panel_down(&s, 360, 220)); /* Themes tab. */
    int before = commands;
    CHECK(appearance_panel_down(&s, 250, 302));
    CHECK(commands == before + 1 && selected_theme == 1 && current == 0);
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
    CHECK(!appearance_panel_key(&s, 't', MOD_CMD));
    CHECK(!appearance_panel_key(&s, 'T', MOD_CMD));
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
        CHECK(pixels[(size_t)400 * OSPI_W + 500] == desktop_theme_color(THEME_PANEL));
        CHECK(pixels[10] == 0); /* Overlay does not paint the menu bar. */
        free(pixels);
    }
    CHECK(appearance_panel_key(&s, 27, 0));
    CHECK(!appearance_panel_active());
    appearance_panel_open_controls(&s);
    CHECK(appearance_panel_key(&s, '\r', 0));
    CHECK(appearance_panel_down(&s, 225, 184)); /* Close box. */
    CHECK(!appearance_panel_active());
    appearance_panel_open_controls(&s);
    CHECK(appearance_panel_context(&s, 512, 360));
    CHECK(s.menu == -1);
    pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Fb fb;
        fb_init(&fb, pixels, OSPI_W, OSPI_H);
        appearance_panel_draw(&s, &fb);
        CHECK(icon_draws == 1);
        CHECK(pixels[(size_t)346 * OSPI_W + 496] == 0xFF123456u);
        CHECK(pixels[(size_t)360 * OSPI_W + 512] == C_BLACK); /* Popup on top. */
        free(pixels);
    }
    CHECK(appearance_panel_down(&s, 520, 372)); /* Open Appearance. */
    CHECK(appearance_panel_context(&s, 800, 550));
    CHECK(appearance_panel_down(&s, 810, 583)); /* Themes context action. */
    before = commands;
    CHECK(appearance_panel_down(&s, 250, 302));
    CHECK(commands == before + 1 && last_command == CMD_THEME_BASE + 1);
    before = commands;
    CHECK(appearance_panel_context(&s, 300, 300));
    CHECK(appearance_panel_key(&s, 27, 0)); /* Dismiss only the popup. */
    CHECK(appearance_panel_active());
    CHECK(appearance_panel_key(&s, '\t', 0)); /* Desktop. */
    CHECK(appearance_panel_context(&s, 300, 300));
    CHECK(appearance_panel_down(&s, 250, 302)); /* Dismiss, do not select underneath. */
    CHECK(commands == before);
    CHECK(appearance_panel_down(&s, 250, 302));
    CHECK(commands == before + 1);
    CHECK(appearance_panel_context(&s, 20, 40)); /* Outside panel: consume without menu. */
    CHECK(appearance_panel_down(&s, 250, 302));
    CHECK(commands == before + 2);
    appearance_panel_close();
    CHECK(!appearance_panel_context(&s, 512, 360));
    selected_theme = 0;
    appearance_panel_open(&s, APPEARANCE_THEMES_TAB);
    int wallpaper_before = current;
    for (int page = 0; page < 2; page++) {
        for (int row = 0; row < 8; row++) {
            CHECK(appearance_panel_down(&s, 250, 281 + row * 29));
            CHECK(selected_theme == page * 8 + row);
            CHECK(last_command == CMD_THEME_BASE + selected_theme);
        }
        CHECK(appearance_panel_down(&s, 740, 532));
    }
    CHECK(current == wallpaper_before); /* Themes never overwrite wallpaper choice. */
    CHECK(appearance_panel_down(&s, 250, 484));
    CHECK(selected_theme == 15); /* Last page is bounded. */
    selected_theme = 2;
    appearance_panel_open(&s, APPEARANCE_THEMES_TAB);
    pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels);
    CHECK(pixels != NULL);
    if (pixels) {
        Fb fb;
        fb_init(&fb, pixels, OSPI_W, OSPI_H);
        appearance_panel_draw(&s, &fb);
        CHECK(pixels[(size_t)174 * OSPI_W + 212] == desktop_theme_color(THEME_FRAME));
        CHECK(pixels[(size_t)400 * OSPI_W + 500] == desktop_theme_color(THEME_PANEL));
        CHECK(pixels[(size_t)300 * OSPI_W + 215] == desktop_theme_color(THEME_MENU));
        free(pixels);
    }
    for (int app = 0; app < 3; app++) {
        appearance_panel_open_controls(&s);
        CHECK(appearance_panel_context(&s, 512, 360));
        Rect regions[2]; CHECK(appearance_panel_regions(&s, regions) == 2);
        s.browser_focused = app == 0; s.guest_focused = app == 1; s.finder_hidden = app == 2;
        appearance_panel_deactivate();
        CHECK(!appearance_panel_active() && !appearance_panel_can_open());
        CHECK(!appearance_panel_key(&s, 27, 0));
        occluded = 1;
        CHECK(!appearance_panel_down(&s, 512, 360));
        occluded = 0;
        if (app != 2) {
            CHECK(appearance_panel_down(&s, 512, 310)); /* Exposed title reactivates Finder. */
            CHECK(!s.browser_focused && !s.guest_focused && appearance_panel_can_open());
        }
        CHECK(appearance_panel_regions(&s, regions) == (app == 2 ? 0 : 1));
        if (app == 2) {
            uint32_t *hidden = calloc((size_t)OSPI_W * OSPI_H, sizeof *hidden);
            CHECK(hidden != NULL);
            if (hidden) {
                Fb fb; fb_init(&fb, hidden, OSPI_W, OSPI_H);
                appearance_panel_draw(&s, &fb);
                int painted = 0;
                for (int i=0;i<OSPI_W*OSPI_H;i++) painted |= hidden[i] != 0;
                CHECK(!painted); free(hidden);
            }
        }
        s.browser_focused = s.guest_focused = s.finder_hidden = 0;
        CHECK(appearance_panel_active() && appearance_panel_can_open());
    }
    appearance_panel_close();
    printf("appearance panel: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

void desktop_menu_track(Session *s) { (void)s; }
Session *desktop_menu_tracked(void) { return NULL; }
void desktop_menu_note_unavailable(int c) { (void)c; }
const char *desktop_menu_reason(const Session *s, int c) { (void)s; (void)c; return NULL; }
int desktop_command_enabled(Session *s, int cmd) { (void)s; (void)cmd; return 0; }

int finder_apps_enabled(const Session *s, int command) { (void)s; (void)command; return 0; }
int finder_apps_media_running(const Session *s) { (void)s; return 0; }
int finder_command_enabled(const Session *s, int cmd) { (void)s; (void)cmd; return 0; }
