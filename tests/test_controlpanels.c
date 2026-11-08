#include "control_panel_store.h"
#include "control_panels.h"
#include "appearance_panel.h"
#include "desktop_assets.h"
#include "desktop_theme.h"
#include "finder_edit.h"
#include "menu_navigation.h"
#include "os999_cdev.h"
#include "os999_startup.h"
#include "ospi.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void click_at(Session *s, int x, int y, int clicks) {
    session_down(s, x, y, clicks);
    session_up(s, x, y);
}

static int copy_file_bytes(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[4096];
    size_t n;
    int err = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { err = -1; break; }
    }
    fclose(in);
    if (fclose(out) != 0) err = -1;
    return err;
}

static void test_store_crud_and_safety(void) {
    char root[] = "/tmp/os999-cp-store-XXXXXX";
    CHECK(mkdtemp(root) != NULL);

    os999_cp_store_t store;
    CHECK(os999_cp_store_init(&store, root) == 0);
    CHECK(os999_cp_store_load(&store) == 0);
    CHECK(store.count == 0);

    char json_path[OSPI_PATH + 64];
    snprintf(json_path, sizeof json_path, "%s/%s", root, OS999_CP_STORE_FILENAME);
    struct stat st;
    CHECK(stat(json_path, &st) != 0); /* Lazy: never creates file before save */

    CHECK(os999_cp_store_set_int(&store, "general", "cursor_blink_ms", 250) == 0);
    CHECK(os999_cp_store_set_bool(&store, "sound", "startup_chime", 0) == 0);
    CHECK(os999_cp_store_set_str(&store, "sound", "alert_sound", "alert.droplet") == 0);
    CHECK(os999_cp_store_set_str(&store, "notes", "escaped", "line1\n\"quoted\"\\tab\tend") == 0);
    CHECK(os999_cp_store_set_int(&store, "../bad", "k", 1) == -1);
    CHECK(os999_cp_store_save(&store) == 0);
    os999_cp_store_destroy(&store);

    os999_cp_store_t reloaded;
    CHECK(os999_cp_store_init(&reloaded, root) == 0);
    CHECK(os999_cp_store_load(&reloaded) == 0);
    CHECK(os999_cp_store_get_int(&reloaded, "general", "cursor_blink_ms", 530) == 250);
    CHECK(os999_cp_store_get_bool(&reloaded, "sound", "startup_chime", 1) == 0);
    char buf[128];
    CHECK(os999_cp_store_get_str(&reloaded, "sound", "alert_sound", buf, sizeof buf, "") == 0);
    CHECK(strcmp(buf, "alert.droplet") == 0);
    CHECK(os999_cp_store_get_str(&reloaded, "notes", "escaped", buf, sizeof buf, "") == 0);
    CHECK(strcmp(buf, "line1\n\"quoted\"\\tab\tend") == 0);

    /* Truncated/corrupt JSON must fail cleanly without corrupting in-memory values. */
    FILE *bad = fopen(json_path, "wb");
    CHECK(bad != NULL);
    if (bad) {
        fputs("{\"version\":1,\"panels\":{\"general\":{\"cursor_blink_ms\":", bad);
        fclose(bad);
    }
    CHECK(os999_cp_store_load(&reloaded) == -1);
    CHECK(os999_cp_store_get_int(&reloaded, "general", "cursor_blink_ms", 0) == 250);

    /* Symlinked config file must be rejected on load and save. */
    unlink(json_path);
    char target[OSPI_PATH + 64];
    snprintf(target, sizeof target, "%s/outside-target.json", root);
    FILE *tf = fopen(target, "wb");
    if (tf) { fputs("{\"version\":1,\"panels\":{}}\n", tf); fclose(tf); }
    CHECK(symlink(target, json_path) == 0);
    CHECK(os999_cp_store_load(&reloaded) == -1);
    CHECK(os999_cp_store_save(&reloaded) == -1);
    unlink(json_path);
    unlink(target);
    os999_cp_store_destroy(&reloaded);
    CHECK(rmdir(root) == 0);
}

typedef struct {
    os999_cp_store_t *store;
    int id;
    int errors;
} ThreadArg;

static void *store_worker(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    char panel[16], str_out[64];
    snprintf(panel, sizeof panel, "worker_%d", ta->id);
    for (int i = 0; i < 150; i++) {
        if (os999_cp_store_set_int(ta->store, panel, "counter", i) != 0) ta->errors++;
        if (os999_cp_store_get_int(ta->store, panel, "counter", -1) < 0) ta->errors++;
        if (os999_cp_store_set_str(ta->store, panel, "label", "ok") != 0) ta->errors++;
        if (os999_cp_store_get_str(ta->store, panel, "label", str_out, sizeof str_out, "") != 0)
            ta->errors++;
        if ((i % 25) == 0 && os999_cp_store_save(ta->store) != 0) ta->errors++;
    }
    return NULL;
}

static void test_store_multithreaded(void) {
    char root[] = "/tmp/os999-cp-mt-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    os999_cp_store_t store;
    CHECK(os999_cp_store_init(&store, root) == 0);

    pthread_t threads[4];
    ThreadArg args[4];
    for (int i = 0; i < 4; i++) {
        args[i] = (ThreadArg){.store = &store, .id = i, .errors = 0};
        CHECK(pthread_create(&threads[i], NULL, store_worker, &args[i]) == 0);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
        CHECK(args[i].errors == 0);
    }
    CHECK(os999_cp_store_save(&store) == 0);
    os999_cp_store_destroy(&store);

    os999_cp_store_t verify;
    CHECK(os999_cp_store_init(&verify, root) == 0);
    CHECK(os999_cp_store_load(&verify) == 0);
    for (int i = 0; i < 4; i++) {
        char panel[16];
        snprintf(panel, sizeof panel, "worker_%d", i);
        CHECK(os999_cp_store_get_int(&verify, panel, "counter", -1) == 149);
    }
    os999_cp_store_destroy(&verify);

    char json_path[OSPI_PATH + 64];
    snprintf(json_path, sizeof json_path, "%s/%s", root, OS999_CP_STORE_FILENAME);
    unlink(json_path);
    CHECK(rmdir(root) == 0);
}

static int g_hook_vol = -1, g_hook_muted = -1;
static char g_hook_sound[48];
static void spy_set_vol(int pct) { g_hook_vol = pct; }
static void spy_set_muted(int m) { g_hook_muted = m; }
static void spy_play_sound(const char *name) {
    snprintf(g_hook_sound, sizeof g_hook_sound, "%s", name ? name : "");
}

static const MenuItem *find_menu_cmd(int menu, int cmd) {
    for (int i = 0; i < menu_nitems(menu); i++) {
        const MenuItem *it = menu_at(menu, i);
        if (it && it->cmd == cmd) return it;
    }
    return NULL;
}

static int region_has_color(const uint32_t *px, int w, Rect r, uint32_t color) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++)
            if (px[y * w + x] == color) return 1;
    }
    return 0;
}

static void test_plugin_container_and_modules(const char *plugin_so) {
    os999_cdev_unregister_all();
    os999_cdev_t bad_ver = {.api_version = 999, .id = "bad", .title = "Bad"};
    CHECK(os999_cdev_register(NULL) == -1);
    CHECK(os999_cdev_register(&bad_ver) == -1);
    CHECK(os999_cdev_load_plugin("/nonexistent/missing.cdev") == -1);

    char root[] = "/tmp/os999-cp-session-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);

    char cp_dir[OSPI_PATH + 64], dst_plugin[OSPI_PATH + 96];
    snprintf(cp_dir, sizeof cp_dir, "%s/System Folder/Control Panels", root);
    CHECK(volume_mkdir_p(cp_dir) == 0);
    snprintf(dst_plugin, sizeof dst_plugin, "%s/energy_saver.cdev", cp_dir);
    CHECK(copy_file_bytes(plugin_so, dst_plugin) == 0);

    const ControlPanelsAudioHooks hooks = {
        .set_volume = spy_set_vol,
        .set_muted = spy_set_muted,
        .play_sound = spy_play_sound
    };
    control_panels_set_audio_hooks(&hooks);

    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return;

    /* Built-ins + dynamically scanned .cdev shared library must be registered. */
    CHECK(os999_cdev_count() == 5);
    CHECK(os999_cdev_find("general") != NULL);
    CHECK(os999_cdev_find("sound") != NULL);
    CHECK(os999_cdev_find("appearance") != NULL);
    CHECK(os999_cdev_find("energy_saver") != NULL);
    CHECK(os999_cdev_register(os999_cdev_general_module()) == -1); /* Duplicate rejected */

    menu_update(s);
    CHECK(find_menu_cmd(MENU_PANELS, CMD_CONTROL_PANELS) != NULL);
    CHECK(find_menu_cmd(MENU_PANELS, CMD_CP_GENERAL) != NULL);
    CHECK(find_menu_cmd(MENU_PANELS, CMD_CP_SOUND) != NULL);
    CHECK(!(find_menu_cmd(MENU_PANELS, CMD_CP_GENERAL)->flags & MI_DISABLED));
    CHECK(!(find_menu_cmd(MENU_PANELS, CMD_CP_SOUND)->flags & MI_DISABLED));

    uint32_t *px = calloc((size_t)(OSPI_W * OSPI_H), sizeof *px);
    CHECK(px != NULL);

    /* Open Control Panels container grid and verify keyboard/category/mouse/drag. */
    session_command(s, CMD_CONTROL_PANELS);
    CHECK(control_panels_active() && appearance_panel_active());
    CHECK(control_panels_can_open() == 1);
    CHECK(control_panels_active_module_id() == NULL);
    session_paint(s, px, OSPI_W, OSPI_H);

    Rect regions[2];
    CHECK(appearance_panel_regions(s, regions) == 1);
    Rect r = {regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    CHECK(r.x == 202 && r.y == 164 && r.w == 620 && r.h == 440);

    /* Arrow navigation in grid + Tab category cycling. */
    CHECK(control_panels_selected_index() == 0);
    session_key(s, MENU_KEY_RIGHT, 0);
    CHECK(control_panels_selected_index() == 1);
    session_key(s, MENU_KEY_DOWN, 0); /* three columns: 1 -> 4 now that Extensions is the fifth panel */
    CHECK(control_panels_selected_index() == 4);
    session_key(s, MENU_KEY_UP, 0);
    CHECK(control_panels_selected_index() == 1);
    session_key(s, MENU_KEY_LEFT, 0);
    CHECK(control_panels_selected_index() == 0);

    session_key(s, '\t', 0);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_APPEARANCE);
    session_key(s, '\t', MOD_SHIFT);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_ALL);

    /* Filter to Hardware category by clicking pill 4 and open dynamic Energy Saver plugin. */
    click_at(s, r.x + 16 + 4 * 94 + 45, r.y + 40, 1);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_HARDWARE);
    session_key(s, '\r', 0);
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "energy_saver") == 0);
    CHECK(control_panels_can_open() == 0);

    session_paint(s, px, OSPI_W, OSPI_H);
    Rect c = {r.x + 16, r.y + 60, r.w - 32, r.h - 76};
    CHECK(region_has_color(px, OSPI_W, c, 0xFF28A448u)); /* Plugin rendered green meter */

    click_at(s, c.x + 40, c.y + 30, 1); /* sleep_minutes: 15 -> 20, plays window.zoom */
    CHECK(strcmp(s->sound_event, "window.zoom") == 0);
    session_key(s, '+', 0); /* sleep_minutes: 20 -> 25 */
    session_key(s, 'p', 0); /* low_power: 0 -> 1 */
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(region_has_color(px, OSPI_W, c, 0xFF2088C8u)); /* Plugin switched to blue meter */

    /* Return to grid via < All Panels button, reset category to All, test double-click open. */
    click_at(s, r.x + 60, r.y + 40, 1);
    CHECK(control_panels_active_module_id() == NULL);
    click_at(s, r.x + 16 + 45, r.y + 40, 1);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_ALL);
    click_at(s, r.x + 16 + 10 + 90, r.y + 60 + 12 + 70, 2); /* Double-click slot 0 (General) */
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "general") == 0);

    /* Test dragging Control Panels window by title bar and restoring position on reopen. */
    session_down(s, r.x + 200, r.y + 10, 1);
    session_move(s, r.x + 240, r.y + 35);
    session_up(s, r.x + 240, r.y + 35);
    CHECK(appearance_panel_regions(s, regions) == 1);
    CHECK(regions[0].x == 242 && regions[0].y == 189);
    session_key(s, 'w', MOD_CMD);
    CHECK(!control_panels_active());

    /* Open General Controls directly via CMD_CP_GENERAL. */
    session_command(s, CMD_CP_GENERAL);
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "general") == 0);
    CHECK(appearance_panel_regions(s, regions) == 1);
    r = (Rect){regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    c = (Rect){r.x + 16, r.y + 60, r.w - 32, r.h - 76};

    /* Verify Desktop Pattern live pixel updates on Plain Desktop. */
    session_paint(s, px, OSPI_W, OSPI_H);
    click_at(s, c.x + 16 + 3 * 68 + 28, c.y + 150, 1); /* Pattern 3: Solid Tone */
    CHECK(s->desktop_pattern == 3);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(px[721 * OSPI_W + 24] == px[721 * OSPI_W + 25]); /* Solid: adjacent pixels match */
    click_at(s, c.x + 16 + 1 * 68 + 28, c.y + 150, 1); /* Pattern 1: Fine Grid */
    CHECK(s->desktop_pattern == 1);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(px[721 * OSPI_W + 24] != px[721 * OSPI_W + 25]); /* Grid line at x=24 vs cell at x=25 */

    /* Step color theme via keyboard ']' and click Cursor Blink "Fast" (250 ms) & Menu Blink 1x. */
    int prev_theme = desktop_theme_current();
    session_key(s, ']', 0);
    CHECK(desktop_theme_current() != prev_theme);
    click_at(s, c.x + 118 + 3 * 70 + 32, c.y + 268, 1); /* Fast: 250 ms */
    CHECK(s->cursor_blink_ms == 250 && s->cursor_blink_set == 1);
    click_at(s, c.x + 118 + 1 * 70 + 32, c.y + 308, 1); /* Menu Blink: 1x */
    CHECK(s->menu_blink_count == 1);

    /* Verify Finder inline rename caret honors s->cursor_blink_ms (250 ms vs Off 0 ms). */
    session_key(s, 'w', MOD_CMD);
    Window *fw = &s->win[s->nwin - 1];
    CHECK(fw->nitems > 0);
    fw->items[0].selected = 1;
    s->desktop_focus = 0;
    s->desk_sel = -1;
    session_tick(s, 1000);
    CHECK(finder_edit_return(s) == 1);
    session_key(s, MENU_KEY_RIGHT, 0); /* collapse selection so caret draws */
    Rect er;
    CHECK(session_edit_rect(s, &er) == 1);
    s->input_now_ms = 1100; /* elapsed 100 ms -> (100/250)%2 == 0 -> caret visible */
    session_paint(s, px, OSPI_W, OSPI_H);
    uint32_t caret_visible_hash = 0, caret_hidden_hash = 0;
    for (int y = er.y; y < er.y + er.h; y++)
        for (int x = er.x; x < er.x + er.w; x++)
            caret_visible_hash = caret_visible_hash * 33u + px[y * OSPI_W + x];
    s->input_now_ms = 1300; /* elapsed 300 ms -> (300/250)%2 == 1 -> caret hidden! */
    session_paint(s, px, OSPI_W, OSPI_H);
    for (int y = er.y; y < er.y + er.h; y++)
        for (int x = er.x; x < er.x + er.w; x++)
            caret_hidden_hash = caret_hidden_hash * 33u + px[y * OSPI_W + x];
    CHECK(caret_visible_hash != caret_hidden_hash);
    finder_edit_commit(s);

    /* Open Sound & Startup directly via CMD_CP_SOUND and test volume, alerts, startup prefs. */
    session_command(s, CMD_CP_SOUND);
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "sound") == 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(appearance_panel_regions(s, regions) == 1);
    r = (Rect){regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    c = (Rect){r.x + 16, r.y + 60, r.w - 32, r.h - 76};

    /* Drag volume slider from ~25% to ~65%. */
    session_down(s, c.x + 62 + 65, c.y + 36, 1);
    session_move(s, c.x + 62 + 169, c.y + 36);
    session_up(s, c.x + 62 + 169, c.y + 36);
    CHECK(s->system_volume == 65 && g_hook_vol == 65);

    /* Select "Droplet" (row 2) in Alert Sounds list. */
    g_hook_sound[0] = 0;
    click_at(s, c.x + 80, c.y + 121 + 2 * 25 + 12, 1);
    CHECK(strcmp(s->alert_sound, "alert.droplet") == 0);
    CHECK(strcmp(s->sound_event, "alert.droplet") == 0);
    CHECK(strcmp(g_hook_sound, "alert.droplet") == 0);

    /* Toggle Mute on then off via checkbox. */
    click_at(s, c.x + 480, c.y + 36, 1);
    CHECK(s->sound_muted == 1 && g_hook_muted == 1);
    click_at(s, c.x + 480, c.y + 36, 1);
    CHECK(s->sound_muted == 0 && g_hook_muted == 0);

    /* Toggle Classic Startup Chime off and Startup Animation off. */
    CHECK(s->startup_chime_enabled == 1);
    click_at(s, c.x + 420, c.y + 136, 1);
    CHECK(s->startup_chime_enabled == 0);
    click_at(s, c.x + 420, c.y + 214, 1);
    CHECK(s->host_startup_enabled == 0);
    CHECK(os999_startup_pref_read(root) == 0);

    int saved_theme = desktop_theme_current();
    session_key(s, 'w', MOD_CMD);
    session_destroy(s);

    /* Re-open a fresh Session on the same volume and verify all settings persisted! */
    Session *s2 = session_create(root);
    CHECK(s2 != NULL);
    if (s2) {
        CHECK(desktop_theme_current() == saved_theme);
        CHECK(s2->desktop_pattern == 1);
        CHECK(s2->cursor_blink_ms == 250);
        CHECK(s2->menu_blink_count == 1);
        CHECK(s2->system_volume == 65);
        CHECK(s2->sound_muted == 0);
        CHECK(s2->startup_chime_enabled == 0);
        CHECK(s2->host_startup_enabled == 0);
        CHECK(strcmp(s2->alert_sound, "alert.droplet") == 0);
        os999_cp_store_t *st2 = control_panels_store(s2);
        CHECK(st2 != NULL);
        CHECK(os999_cp_store_get_int(st2, "energy_saver", "sleep_minutes", 0) == 25);
        CHECK(os999_cp_store_get_bool(st2, "energy_saver", "low_power", 0) == 1);
        session_destroy(s2);
    }

    free(px);
    control_panels_set_audio_hooks(NULL);
    os999_cdev_unregister_all();
}

int main(int argc, char **argv) {
    const char *plugin_so = argc > 1 ? argv[1] : "build/fixture_cdev.so";
    const char *icons_path = argc > 2 ? argv[2] : "assets/icons/ospi-icons.icl8";
    CHECK(desktop_assets_load(icons_path));

    test_store_crud_and_safety();
    test_store_multithreaded();
    test_plugin_container_and_modules(plugin_so);

    desktop_assets_reset();
    printf("controlpanels: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
