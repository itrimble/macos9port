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

#include <dirent.h>
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

static void rm_tree(const char *path) {
    struct stat st;
    if (!path || lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) { unlink(path); return; }
    DIR *d = opendir(path);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
            char sub[OSPI_PATH * 2];
            int n = snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            if (n > 0 && (size_t)n < sizeof sub) rm_tree(sub);
        }
        closedir(d);
    }
    rmdir(path);
}

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
    snprintf(dst_plugin, sizeof dst_plugin, "%s/date_time.cdev", cp_dir);
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

    /* 10 built-ins (incl. Bluetooth and Extensions Manager) + 1 dynamically scanned .cdev shared library. */
    CHECK(os999_cdev_count() == 11);
    const char *const expected_ids[11] = {
        "general", "sound", "appearance", "monitors", "keyboard_mouse",
        "energy_saver", "memory", "network", "bluetooth", "extensions", "date_time"
    };
    for (int i = 0; i < 11; i++) CHECK(os999_cdev_find(expected_ids[i]) != NULL);
    CHECK(os999_cdev_register(os999_cdev_general_module()) == -1); /* Duplicate rejected */

    menu_update(s);
    const int panel_cmds[] = {
        CMD_CONTROL_PANELS, CMD_CP_GENERAL, CMD_CP_SOUND, CMD_CP_MONITORS,
        CMD_CP_KEYBOARD, CMD_CP_ENERGY, CMD_CP_MEMORY, CMD_CP_NETWORK, CMD_CP_BLUETOOTH, CMD_CP_EXTENSIONS
    };
    for (unsigned i = 0; i < sizeof panel_cmds / sizeof *panel_cmds; i++) {
        const MenuItem *mi = find_menu_cmd(MENU_PANELS, panel_cmds[i]);
        CHECK(mi != NULL && !(mi->flags & MI_DISABLED));
    }

    uint32_t *px = calloc((size_t)(OSPI_W * OSPI_H), sizeof *px);
    CHECK(px != NULL);

    /* Open Control Panels container grid and verify keyboard/category/wheel/scrollbar/drag. */
    session_command(s, CMD_CONTROL_PANELS);
    CHECK(control_panels_active() && appearance_panel_active());
    CHECK(control_panels_can_open() == 1);
    CHECK(control_panels_active_module_id() == NULL);
    session_paint(s, px, OSPI_W, OSPI_H);

    Rect regions[2];
    CHECK(appearance_panel_regions(s, regions) == 1);
    Rect r = {regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    CHECK(r.x == 202 && r.y == 164 && r.w == 620 && r.h == 440);
    {
        Fb clip_fb;
        for (int i = 0; i < OSPI_W * OSPI_H; i++) px[i] = 0xFF123456u;
        fb_init(&clip_fb, px, OSPI_W, OSPI_H);
        control_panels_draw(s, &clip_fb);
        Rect below = {r.x, regions[0].y + regions[0].h, r.w, 80};
        for (int y = below.y; y < below.y + below.h; y++)
            for (int x = below.x; x < below.x + below.w; x++)
                if (px[y * OSPI_W + x] != 0xFF123456u) { CHECK(0); break; }
        session_paint(s, px, OSPI_W, OSPI_H);
    }

    CHECK(control_panels_selected_index() == 0);
    session_key(s, MENU_KEY_RIGHT, 0);
    CHECK(control_panels_selected_index() == 1);
    session_key(s, MENU_KEY_DOWN, 0);
    CHECK(control_panels_selected_index() == 4);
    session_key(s, FINDER_KEY_END, 0);
    CHECK(control_panels_selected_index() == 10); /* End selects the last of 11 panels; auto-scrolls it into view */
    session_key(s, FINDER_KEY_HOME, 0);
    CHECK(control_panels_selected_index() == 0);

    session_key(s, '\t', 0);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_APPEARANCE);
    session_key(s, '\t', MOD_SHIFT);
    CHECK(control_panels_category_filter() == OS999_CDEV_CAT_ALL);

    /* Open dynamic Date & Time plugin directly and verify render + event + save. */
    CHECK(control_panels_open_module(s, "date_time") == 0);
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "date_time") == 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    Rect c = {r.x + 16, r.y + 60, r.w - 32, r.h - 76};
    CHECK(region_has_color(px, OSPI_W, c, 0xFF28A448u));
    click_at(s, c.x + 40, c.y + 30, 1); /* utc_offset_min: -360 -> -300 */
    CHECK(s->sound_muted && s->sound_event[0] == 0); /* Fresh volumes suppress effects. */
    session_key(s, '+', 0); /* -300 -> -240 */
    session_key(s, 't', 0); /* use_24_hour: 1 */
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(region_has_color(px, OSPI_W, c, 0xFF2088C8u));

    /* Return to grid via < All Panels button and test double-click open + window drag. */
    click_at(s, r.x + 60, r.y + 40, 1);
    CHECK(control_panels_active_module_id() == NULL);
    click_at(s, r.x + 16 + 10 + 90, r.y + 60 + 12 + 70, 2); /* Double-click slot 0 (General) */
    CHECK(control_panels_active_module_id() &&
          strcmp(control_panels_active_module_id(), "general") == 0);
    session_down(s, r.x + 200, r.y + 10, 1);
    session_move(s, r.x + 240, r.y + 35);
    session_up(s, r.x + 240, r.y + 35);
    CHECK(appearance_panel_regions(s, regions) == 1);
    CHECK(regions[0].x == 242 && regions[0].y == 189);
    session_key(s, 'w', MOD_CMD);
    CHECK(!control_panels_active());

    /* 1. General Controls: Desktop Pattern, Color Theme, Cursor & Menu Blink. */
    session_command(s, CMD_CP_GENERAL);
    CHECK(appearance_panel_regions(s, regions) == 1);
    r = (Rect){regions[0].x, regions[0].y, regions[0].w - 3, regions[0].h - 3};
    c = (Rect){r.x + 16, r.y + 60, r.w - 32, r.h - 76};
    click_at(s, c.x + 16 + 3 * 68 + 28, c.y + 150, 1); /* Solid Tone */
    CHECK(s->desktop_pattern == 3);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(px[721 * OSPI_W + 24] == px[721 * OSPI_W + 25]);
    click_at(s, c.x + 16 + 1 * 68 + 28, c.y + 150, 1); /* Fine Grid */
    CHECK(s->desktop_pattern == 1);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(px[721 * OSPI_W + 24] != px[721 * OSPI_W + 25]);
    int prev_theme = desktop_theme_current();
    session_key(s, ']', 0);
    CHECK(desktop_theme_current() != prev_theme);
    click_at(s, c.x + 118 + 3 * 70 + 32, c.y + 268, 1); /* Fast: 250 ms */
    click_at(s, c.x + 118 + 1 * 70 + 32, c.y + 308, 1); /* Menu Blink: 1x */
    CHECK(s->cursor_blink_ms == 250 && s->menu_blink_count == 1);
    session_key(s, 'w', MOD_CMD);

    /* Verify Finder inline rename caret honors s->cursor_blink_ms (250 ms). */
    Window *fw = &s->win[s->nwin - 1];
    fw->items[0].selected = 1;
    s->desktop_focus = 0; s->desk_sel = -1;
    session_tick(s, 1000);
    CHECK(finder_edit_return(s) == 1);
    session_key(s, MENU_KEY_RIGHT, 0);
    Rect er;
    CHECK(session_edit_rect(s, &er) == 1);
    s->input_now_ms = 1100; session_paint(s, px, OSPI_W, OSPI_H);
    uint32_t h_vis = 0, h_hid = 0;
    for (int y = er.y; y < er.y + er.h; y++)
        for (int x = er.x; x < er.x + er.w; x++) h_vis = h_vis * 33u + px[y * OSPI_W + x];
    s->input_now_ms = 1300; session_paint(s, px, OSPI_W, OSPI_H);
    for (int y = er.y; y < er.y + er.h; y++)
        for (int x = er.x; x < er.x + er.w; x++) h_hid = h_hid * 33u + px[y * OSPI_W + x];
    CHECK(h_vis != h_hid);
    finder_edit_commit(s);

    /* 2. Sound & Startup: Volume drag, Alert selection, Mute, Startup chime & intro. */
    session_command(s, CMD_CP_SOUND);
    session_down(s, c.x + 62 + 65, c.y + 36, 1);
    session_move(s, c.x + 62 + 169, c.y + 36);
    session_up(s, c.x + 62 + 169, c.y + 36);
    CHECK(s->system_volume == 65 && g_hook_vol == 65);
    click_at(s, c.x + 80, c.y + 121 + 2 * 25 + 12, 1); /* Droplet */
    CHECK(strcmp(s->alert_sound, "alert.droplet") == 0 && strcmp(g_hook_sound, "alert.droplet") == 0);
    CHECK(s->startup_chime_enabled == 0);
    click_at(s, c.x + 420, c.y + 136, 1); /* Explicitly enable chime. */
    CHECK(s->startup_chime_enabled == 1);
    click_at(s, c.x + 420, c.y + 136, 1); /* Startup chime off */
    click_at(s, c.x + 420, c.y + 214, 1); /* Startup intro off */
    CHECK(s->startup_chime_enabled == 0 && s->host_startup_enabled == 0);

    /* 3. Monitors & Display: Grayscale quantization, Brightness, Gamma, Scanlines. */
    session_command(s, CMD_CP_MONITORS);
    CHECK(strcmp(control_panels_active_module_id(), "monitors") == 0);
    click_at(s, c.x + 176 + 76, c.y + 56 + 13, 1); /* Grayscale (mode 3) */
    click_at(s, c.x + 408 + 16, c.y + 182 + 12, 1); /* Brightness +5 -> 105% */
    click_at(s, c.x + 112 + 154 + 70, c.y + 218 + 12, 1); /* Trinitron 9300K (gamma 1) */
    click_at(s, c.x + 40, c.y + 306, 1); /* Scanlines on */
    CHECK(s->display_color_mode == 3 && s->display_brightness == 105 &&
          s->display_gamma == 1 && s->display_scanlines == 1);
    session_paint(s, px, OSPI_W, OSPI_H);
    uint32_t gp = px[300 * OSPI_W + 300];
    CHECK(((gp >> 16) & 255u) == ((gp >> 8) & 255u) && ((gp >> 8) & 255u) == (gp & 255u));

    /* 4. Keyboard & Mouse: Fast repeat, Fast 300ms double-click, 3x wheel, Natural scroll. */
    session_command(s, CMD_CP_KEYBOARD);
    CHECK(strcmp(control_panels_active_module_id(), "keyboard_mouse") == 0);
    click_at(s, c.x + 190 + 3 * 96 + 40, c.y + 36, 1); /* Key repeat: Fast (3) */
    click_at(s, c.x + 190 + 2 * 128 + 50, c.y + 72, 1); /* Delay: Short (250 ms) */
    click_at(s, c.x + 158 + 2 * 64 + 28, c.y + 170, 1); /* Double-click: Fast (300 ms) */
    click_at(s, c.x + 122 + 2 * 76 + 35, c.y + 214, 1); /* Wheel speed: 3x */
    click_at(s, c.x + 50, c.y + 258, 1); /* Natural scroll on */
    CHECK(s->key_repeat_rate == 3 && s->key_repeat_delay_ms == 250 &&
          s->double_click_ms == 300 && s->wheel_speed == 3 && s->natural_scroll == 1);
    s->sound_event[0] = 0;
    s->input_now_ms = 2000; click_at(s, c.x + 478, c.y + 220, 1);
    s->input_now_ms = 2200; click_at(s, c.x + 478, c.y + 220, 1); /* 200 ms <= 300 ms */
    CHECK(strcmp(s->sound_event, "window.zoom") == 0);

    /* 5. Energy Saver: 5 min preset, Conservation profile, idle display dim + key wake. */
    session_command(s, CMD_CP_ENERGY);
    CHECK(strcmp(control_panels_active_module_id(), "energy_saver") == 0);
    click_at(s, c.x + 138 + 37, c.y + 74, 1); /* 5 min preset */
    click_at(s, c.x + 100, c.y + 154 + 32 + 13, 1); /* Conservation (profile 1) */
    CHECK(s->energy_sleep_mins == 5 && s->power_profile == 1 && !s->display_dimmed);
    session_tick(s, s->last_activity_ms + 5u * 60000u + 10u);
    CHECK(s->display_dimmed == 1);
    session_key(s, 'a', 0);
    CHECK(s->display_dimmed == 0);

    /* 6. Memory: Custom 1024 KB cache, 12 MB 68k heap, 16 MB RAM disk. */
    session_command(s, CMD_CP_MEMORY);
    CHECK(strcmp(control_panels_active_module_id(), "memory") == 0);
    click_at(s, c.x + 238 + 18, c.y + 172, 1); /* + cache: 512 -> 1024 KB */
    click_at(s, c.x + 314 + 3 * 66 + 30, c.y + 136, 1); /* 12 MB 68k heap */
    click_at(s, c.x + 306 + 2 * 66 + 30, c.y + 304, 1); /* 16 MB RAM disk */
    CHECK(s->disk_cache_kb == 1024 && s->guest_ram_mb == 12 && s->ram_disk_mb == 16);

    /* 7. TCP/IP & Network: Tailscale transport, 30s timeout, policy persistence. */
    session_command(s, CMD_CP_NETWORK);
    CHECK(strcmp(control_panels_active_module_id(), "network") == 0);
    click_at(s, c.x + 166 + 136 + 64, c.y + 36, 1); /* Connect via: Tailscale (1) */
    click_at(s, c.x + 96 + 2 * 60 + 27, c.y + 262, 1); /* Timeout: 30s */
    CHECK(s->net_connect_via == 1 && s->net_timeout_sec == 30 &&
          s->net_https_only == 1 && s->net_guest_isolated == 1);

    int saved_theme = desktop_theme_current();
    session_paint(s, px, OSPI_W, OSPI_H);
    session_key(s, 'w', MOD_CMD);
    session_destroy(s);

    /* Re-open a fresh Session on the same volume and verify all 9 modules persisted! */
    Session *s2 = session_create(root);
    CHECK(s2 != NULL);
    if (s2) {
        CHECK(desktop_theme_current() == saved_theme);
        CHECK(s2->desktop_pattern == 1 && s2->cursor_blink_ms == 250 && s2->menu_blink_count == 1);
        CHECK(s2->system_volume == 65 && s2->startup_chime_enabled == 0 && s2->host_startup_enabled == 0);
        CHECK(strcmp(s2->alert_sound, "alert.droplet") == 0);
        CHECK(s2->display_color_mode == 3 && s2->display_brightness == 105 &&
              s2->display_gamma == 1 && s2->display_scanlines == 1);
        CHECK(s2->key_repeat_rate == 3 && s2->key_repeat_delay_ms == 250 &&
              s2->double_click_ms == 300 && s2->wheel_speed == 3 && s2->natural_scroll == 1);
        CHECK(s2->energy_sleep_mins == 5 && s2->power_profile == 1);
        CHECK(s2->disk_cache_kb == 1024 && s2->guest_ram_mb == 12 && s2->ram_disk_mb == 16);
        CHECK(s2->net_connect_via == 1 && s2->net_timeout_sec == 30);
        os999_cp_store_t *st2 = control_panels_store(s2);
        CHECK(st2 != NULL);
        CHECK(os999_cp_store_get_int(st2, "date_time", "utc_offset_min", 0) == -240);
        CHECK(os999_cp_store_get_bool(st2, "date_time", "use_24_hour", 0) == 1);
        session_destroy(s2);
    }

    free(px);
    control_panels_set_audio_hooks(NULL);
    os999_cdev_unregister_all();
    rm_tree(root);
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
