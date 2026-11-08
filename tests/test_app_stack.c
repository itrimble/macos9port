#include "session_int.h"
#include "finder_apps.h"
#include "appearance_panel.h"
#include "archive_fs.h"
#include "draw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures, media_running = 1;
static Session *owner;
static Rect media_rect = {200, 160, 400, 300};
enum { MEDIA = 0xFFCC4455u, GUEST = 0xFF2277CCu };
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); } } while (0)
static int running(const Session *s) { return s == owner && media_running; }
static int active(void) { return owner && media_running && owner->media_focused && !owner->media_hidden; }
static int bounds(const Session *s, Rect *r) {
    if (!running(s) || s->media_hidden) return 0;
    *r = media_rect; return 1;
}
static void draw(Session *s, void *pixels) {
    Rect r; if (bounds(s, &r)) fb_fill(pixels, r.x, r.y, r.w, r.h, MEDIA);
}
static void composite(Session *s, uint32_t *pixels) {
    session_paint(s, pixels, OSPI_W, OSPI_H);
    Fb fb; fb_init(&fb, pixels, OSPI_W, OSPI_H);
    Rect g = s->guest_bounds; fb_fill(&fb, g.x, g.y, g.w, g.h, GUEST);
    session_windows_paint_above(s, pixels, OSPI_W, OSPI_H, 2);
}
int main(void) {
    char root[] = "/tmp/os999-app-stack-XXXXXX";
    CHECK(mkdtemp(root) != NULL); CHECK(volume_seed(root) == 0);
    Session *s = session_create(root); CHECK(s != NULL); if (!s) return 1;
    owner = s;
    SessionMediaHooks hooks = {.running=running, .active=active, .bounds=bounds, .draw=draw};
    session_set_media_hooks(&hooks);
    s->win[0].x = 100; s->win[0].y = 100; s->win[0].w = 400; s->win[0].h = 300;
    s->guest_active = s->browser_active = 1; s->guest_bounds = (Rect){300, 220, 400, 300};
    finder_apps_command(s, CMD_APP_FINDER); finder_apps_command(s, CMD_APP_MEDIA);
    finder_apps_command(s, CMD_APP_GUEST);
    CHECK(finder_apps_above(s, 2, 3) && finder_apps_above(s, 3, 1));
    CHECK(finder_apps_above(s, 1, 4));
    uint32_t *pixels = calloc((size_t)OSPI_W * OSPI_H, sizeof *pixels); CHECK(pixels != NULL);
    if (pixels) {
        composite(s, pixels);
        CHECK(pixels[250 * OSPI_W + 350] == GUEST);
        CHECK(pixels[200 * OSPI_W + 250] == MEDIA);
        finder_apps_command(s, CMD_APP_FINDER); composite(s, pixels);
        CHECK(pixels[250 * OSPI_W + 350] != GUEST && pixels[250 * OSPI_W + 350] != MEDIA);
        CHECK(pixels[250 * OSPI_W + 650] == GUEST);
        free(pixels);
    }
    finder_apps_command(s, CMD_APP_BROWSER); finder_apps_command(s, CMD_APP_GUEST);
    CHECK(finder_apps_occluded(s, 4, 350, 250));
    CHECK(!finder_apps_occluded(s, 4, 250, 200)); /* Finder/media are below Browser. */
    CHECK(!finder_apps_guest_hit(s, 1, 350, 10));
    CHECK(finder_apps_guest_hit(s, 1, 350, 250));
    finder_apps_command(s, CMD_APP_HIDE);
    CHECK(s->guest_hidden && s->browser_focused);
    finder_apps_command(s, CMD_APP_HIDE);
    CHECK(s->browser_hidden && finder_apps_front(s) == 1);
    finder_apps_command(s, CMD_APP_SHOW_ALL);
    CHECK(!s->browser_hidden && !s->guest_hidden && finder_apps_front(s) == 1);
    finder_apps_command(s, CMD_APP_BROWSER); finder_apps_command(s, CMD_APP_GUEST);
    int focus = 1; finder_apps_sync(s, 1, "Fixture", &focus);
    finder_apps_sync(s, 0, "", &focus);
    CHECK(!focus && s->browser_focused && !s->guest_active);
    /* A background guest exit must not steal Finder's desktop selection. */
    s->guest_active = 1; finder_apps_command(s, CMD_APP_FINDER);
    finder_apps_sync(s, 1, "Background", &focus);
    s->desktop_focus = 1; s->desk_sel = 1;
    finder_apps_sync(s, 0, "", &focus);
    CHECK(!focus && s->desktop_focus && s->desk_sel == 1 && !s->app_request);
    finder_apps_command(s, CMD_APP_BROWSER); finder_apps_sync(s, 0, "", &focus);
    finder_apps_command(s, CMD_APP_FINDER); finder_apps_sync(s, 0, "", &focus);
    CHECK(s->desktop_focus && s->desk_sel == 1);
    finder_apps_command(s, CMD_APP_MEDIA);
    CHECK(session_overlay_active(s) && !finder_apps_modal(s));
    CHECK(finder_apps_key(s, '\t', MOD_CMD, 1, 0) && s->browser_focused);
    CHECK(finder_apps_key(s, '\t', 0, 0, 0));
    session_command(s, CMD_APPEARANCE_DESKTOP);
    CHECK(appearance_panel_active() && !finder_apps_modal(s));
    Rect regions[2]; CHECK(appearance_panel_regions(s, regions) == 1);
    finder_apps_command(s, CMD_APP_BROWSER);
    CHECK(!appearance_panel_active());
    session_down(s, regions[0].x + 50, regions[0].y + 10, 1);
    CHECK(appearance_panel_active() && finder_apps_front(s) == 1);
    session_down(s, 110, 150, 1); session_up(s, 110, 150);
    CHECK(!appearance_panel_active() && appearance_panel_regions(s, regions) == 1);
    appearance_panel_close(); session_set_media_hooks(NULL); owner = NULL;
    session_destroy(s); CHECK(archive_remove_tree(root) == 0);
    printf("Application stack: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
