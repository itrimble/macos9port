#include "control_panel_store.h"
#include "control_panels.h"
#include "control_panels_int.h"
#include "desktop_assets.h"
#include "desktop_theme.h"
#include "finder_files.h"
#include "modern_network.h"
#include "os999_cdev.h"
#include "os999_startup.h"
#include "ospi.h"
#include "session_int.h"

#include <dirent.h>
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

static int hook_vol = -1, hook_muted = -1;
static char hook_played[48];
static void on_vol(int pct) { hook_vol = pct; }
static void on_mute(int muted) { hook_muted = muted; }
static void on_play(const char *name) { snprintf(hook_played, sizeof hook_played, "%s", name ? name : ""); }

static unsigned avg_luma(const uint32_t *px, int x0, int y0, int w, int h) {
    uint64_t sum = 0;
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            uint32_t p = px[y * OSPI_W + x];
            sum += (((p >> 16) & 255u) * 77u + ((p >> 8) & 255u) * 150u + (p & 255u) * 29u) >> 8;
        }
    }
    return (unsigned)(sum / (uint64_t)(w * h));
}

static void test_general_appearance_sound_monitors(void) {
    char root[] = "/tmp/os999-cp-fx1-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);

    static const ControlPanelsAudioHooks hooks = {on_vol, on_mute, on_play};
    control_panels_set_audio_hooks(&hooks);
    Session *s = session_create(root);
    CHECK(s != NULL);
    uint32_t *px_a = calloc((size_t)OSPI_W * OSPI_H, sizeof *px_a);
    uint32_t *px_b = calloc((size_t)OSPI_W * OSPI_H, sizeof *px_b);
    CHECK(px_a != NULL && px_b != NULL);
    if (!s || !px_a || !px_b) { free(px_a); free(px_b); rm_tree(root); return; }
    while (s->nwin > 0) close_index(s, s->nwin - 1);
    Rect c = cp_content_rect(cp_window_rect(s));

    /* General Controls: Desktop pattern pixels (Grid vs Solid). */
    session_command(s, CMD_CP_GENERAL);
    click_at(s, c.x + 16 + 1 * 68 + 28, c.y + 150, 1);
    control_panels_close();
    session_paint(s, px_a, OSPI_W, OSPI_H);
    int grid_diff = 0;
    for (int x = 40; x < 56; x++)
        if (px_a[90 * OSPI_W + x] != px_a[90 * OSPI_W + 40]) grid_diff = 1;
    CHECK(grid_diff == 1);

    session_command(s, CMD_CP_GENERAL);
    click_at(s, c.x + 16 + 3 * 68 + 28, c.y + 150, 1);
    control_panels_close();
    session_paint(s, px_a, OSPI_W, OSPI_H);
    int solid_diff = 0;
    for (int x = 40; x < 56; x++)
        if (px_a[90 * OSPI_W + x] != px_a[90 * OSPI_W + 40]) solid_diff = 1;
    CHECK(solid_diff == 0);

    /* General Controls: Dialog text caret blink (Fast 250ms vs Off 0ms). */
    session_command(s, CMD_CP_GENERAL);
    click_at(s, c.x + 118 + 3 * 70 + 32, c.y + 268, 1);
    control_panels_close();
    session_command(s, CMD_FIND);
    CHECK(s->dialog == 3 && s->cursor_blink_ms == 250);
    session_text_input(s, "blink");
    session_tick(s, 100);
    session_paint(s, px_a, OSPI_W, OSPI_H);
    session_tick(s, 300);
    session_paint(s, px_b, OSPI_W, OSPI_H);
    CHECK(memcmp(px_a, px_b, (size_t)OSPI_W * OSPI_H * sizeof *px_a) != 0);
    session_key(s, 27, 0);

    session_command(s, CMD_CP_GENERAL);
    click_at(s, c.x + 118 + 0 * 70 + 32, c.y + 268, 1);
    control_panels_close();
    session_command(s, CMD_FIND);
    CHECK(s->dialog == 3 && s->cursor_blink_ms == 0);
    session_text_input(s, "steady");
    session_tick(s, 100);
    session_paint(s, px_a, OSPI_W, OSPI_H);
    session_tick(s, 300);
    session_paint(s, px_b, OSPI_W, OSPI_H);
    CHECK(memcmp(px_a, px_b, (size_t)OSPI_W * OSPI_H * sizeof *px_a) == 0);
    session_key(s, 27, 0);

    /* General Controls: Menu Blink popup flash (3x vs Off). */
    session_command(s, CMD_CP_GENERAL);
    click_at(s, c.x + 118 + 3 * 70 + 32, c.y + 308, 1);
    control_panels_close();
    menu_update(s);
    int clean_menu = -1, clean_row = -1;
    for (int m = 0; m < menu_count() && clean_menu < 0; m++) {
        for (int r = 0; r < menu_nitems(m); r++) {
            const MenuItem *it = menu_at(m, r);
            if (it && it->cmd == CMD_HELP_KEYS && !(it->flags & MI_DISABLED)) {
                clean_menu = m; clean_row = r; break;
            }
        }
    }
    CHECK(clean_menu >= 0 && clean_row >= 0);
    if (clean_menu >= 0) {
        session_tick(s, 1000);
        CHECK(menu_activate(s, clean_menu, clean_row) == 1);
        session_key(s, 27, 0);
        CHECK(s->menu_flash_end_ms == 1270);
        session_tick(s, 1010);
        session_paint(s, px_a, OSPI_W, OSPI_H);
        session_tick(s, 1060);
        session_paint(s, px_b, OSPI_W, OSPI_H);
        CHECK(memcmp(px_a, px_b, (size_t)OSPI_W * OSPI_H * sizeof *px_a) != 0);
        session_tick(s, 1300);
        CHECK(s->menu_flash_end_ms == 0);
        session_command(s, CMD_CP_GENERAL);
        click_at(s, c.x + 118 + 32, c.y + 308, 1);
        control_panels_close();
        menu_update(s);
        session_tick(s, 2000);
        CHECK(menu_activate(s, clean_menu, clean_row) == 1);
        session_key(s, 27, 0);
        CHECK(s->menu_flash_end_ms == 0);
    }

    /* Appearance: Color theme switching updates live rendered chrome. */
    CHECK(control_panels_open_module(s, "appearance") == 0);
    click_at(s, c.x + 180, c.y + 16, 1);
    click_at(s, c.x + 80, c.y + 42 + 2 * 26 + 12, 1);
    CHECK(desktop_theme_current() == 2);
    click_at(s, c.x + 80, c.y + 42 + 12, 1);
    CHECK(desktop_theme_current() == 0);

    /* Sound & Startup: live audio hooks, mute gate, alert sound, startup file sync. */
    session_command(s, CMD_CP_SOUND);
    hook_vol = -1; hook_muted = -1; hook_played[0] = 0;
    click_at(s, c.x + 32, c.y + 36, 1);
    click_at(s, c.x + 32, c.y + 36, 1);
    CHECK(hook_vol == 60 && s->system_volume == 60 && s->volume_sync_request == 1);
    click_at(s, c.x + 450, c.y + 36, 1);
    CHECK(hook_muted == 1 && s->sound_muted == 1 && s->host_sound_enabled == 0);
    hook_played[0] = 0;
    click_at(s, c.x + 80, c.y + 270, 1);
    CHECK(hook_played[0] == 0);
    click_at(s, c.x + 450, c.y + 36, 1);
    CHECK(hook_muted == 0 && s->sound_muted == 0);
    click_at(s, c.x + 80, c.y + 121 + 3 * 25 + 12, 1);
    CHECK(strcmp(s->alert_sound, "alert.indigo") == 0);
    CHECK(strcmp(hook_played, "alert.indigo") == 0);
    int intro_before = os999_startup_pref_read(root);
    s->startup_sync_request = 0;
    click_at(s, c.x + 380, c.y + 214, 1);
    CHECK(s->startup_sync_request == 1);
    CHECK(os999_startup_pref_read(root) == !intro_before);

    /* Monitors & Display: RGB565, RGB332/216, Grayscale, Brightness, Gamma, Scanlines. */
    session_command(s, CMD_CP_MONITORS);
    click_at(s, c.x + 14 + 162 + 40, c.y + 36, 1); /* 16-bit */
    session_paint(s, px_a, OSPI_W, OSPI_H);
    uint32_t p16 = px_a[90 * OSPI_W + 40];
    unsigned r16 = (p16 >> 16) & 255u, g16 = (p16 >> 8) & 255u, b16 = p16 & 255u;
    CHECK((r16 & 7u) == (r16 >> 5) && (g16 & 3u) == (g16 >> 6) && (b16 & 7u) == (b16 >> 5));

    click_at(s, c.x + 14 + 40, c.y + 68, 1); /* 256 Colors */
    session_paint(s, px_a, OSPI_W, OSPI_H);
    uint32_t p8 = px_a[90 * OSPI_W + 40];
    CHECK((((p8 >> 16) & 255u) % 51u) == 0 && (((p8 >> 8) & 255u) % 51u) == 0 && ((p8 & 255u) % 51u) == 0);

    click_at(s, c.x + 14 + 162 + 40, c.y + 68, 1); /* Grayscale */
    session_paint(s, px_a, OSPI_W, OSPI_H);
    for (int i = 0; i < OSPI_W * OSPI_H; i += 997) {
        unsigned r = (px_a[i] >> 16) & 255u, g = (px_a[i] >> 8) & 255u, b = px_a[i] & 255u;
        if (r != g || g != b) { CHECK(r == g && g == b); break; }
    }

    click_at(s, c.x + 14 + 40, c.y + 36, 1); /* Back to 32-bit */
    for (int i = 0; i < 4; i++) click_at(s, c.x + 128, c.y + 194, 1); /* 80% brightness */
    session_paint(s, px_a, OSPI_W, OSPI_H);
    unsigned dark_luma = avg_luma(px_a, 32, 72, 32, 32);
    click_at(s, c.x + 530, c.y + 194, 1); /* Reset 100% */
    for (int i = 0; i < 3; i++) click_at(s, c.x + 424, c.y + 194, 1); /* 115% brightness */
    session_paint(s, px_b, OSPI_W, OSPI_H);
    CHECK(avg_luma(px_b, 32, 72, 32, 32) > dark_luma + 20);
    click_at(s, c.x + 530, c.y + 194, 1);

    click_at(s, c.x + 112 + 154 + 50, c.y + 230, 1); /* Trinitron 9300K */
    session_paint(s, px_a, OSPI_W, OSPI_H);
    click_at(s, c.x + 112 + 2 * 154 + 50, c.y + 230, 1); /* Warm 6500K */
    session_paint(s, px_b, OSPI_W, OSPI_H);
    CHECK(((px_a[90 * OSPI_W + 40] & 255u) > (px_b[90 * OSPI_W + 40] & 255u)));
    click_at(s, c.x + 530, c.y + 194, 1);

    click_at(s, c.x + 40, c.y + 306, 1); /* Scanlines ON */
    control_panels_close();
    session_paint(s, px_a, OSPI_W, OSPI_H);
    CHECK((px_a[90 * OSPI_W + 40] & 255u) > (px_a[91 * OSPI_W + 40] & 255u));

    session_destroy(s);
    control_panels_set_audio_hooks(NULL);
    os999_cdev_unregister_all();
    free(px_a);
    free(px_b);
    rm_tree(root);
}

static void test_keyboard_energy_memory_network(void) {
    char root[] = "/tmp/os999-cp-fx2-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    for (int i = 0; i < 24; i++) {
        char p[OSPI_PATH + 64];
        snprintf(p, sizeof p, "%s/Documents/item_%02d.txt", root, i);
        FILE *f = fopen(p, "wb");
        if (f) { fprintf(f, "doc %d\n", i); fclose(f); }
    }

    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) { rm_tree(root); return; }
    Rect c = cp_content_rect(cp_window_rect(s));

    /* Keyboard & Mouse: Typematic repeat (Fast + 250ms delay vs Off). */
    session_command(s, CMD_CP_KEYBOARD);
    click_at(s, c.x + 190 + 3 * 96 + 40, c.y + 36, 1);
    click_at(s, c.x + 190 + 2 * 128 + 60, c.y + 72, 1);
    CHECK(s->key_repeat_rate == 3 && s->key_repeat_delay_ms == 250);
    control_panels_close();

    session_command(s, CMD_FIND);
    session_text_input(s, "x");
    session_key_hold(s, 'x', 0, "x", 1000);
    session_tick(s, 1240);
    CHECK(strcmp(s->edit_text, "x") == 0);
    session_tick(s, 1250);
    CHECK(strcmp(s->edit_text, "xx") == 0);
    session_tick(s, 1280);
    CHECK(strcmp(s->edit_text, "xx") == 0);
    session_tick(s, 1290);
    CHECK(strcmp(s->edit_text, "xxx") == 0);
    session_key_release(s, 'x');
    session_tick(s, 1400);
    CHECK(strcmp(s->edit_text, "xxx") == 0);
    session_key(s, 27, 0);

    session_command(s, CMD_CP_KEYBOARD);
    click_at(s, c.x + 190 + 40, c.y + 36, 1); /* Off */
    click_at(s, c.x + 158 + 2 * 64 + 28, c.y + 170, 1); /* Double-click: Fast 300ms */
    control_panels_close();

    session_command(s, CMD_FIND);
    session_text_input(s, "y");
    session_key_hold(s, 'y', 0, "y", 2000);
    session_tick(s, 3000);
    CHECK(strcmp(s->edit_text, "y") == 0);
    session_key(s, 27, 0);

    /* Keyboard & Mouse: Authoritative double-click threshold in Finder. */
    CHECK(s->nwin == 1);
    int doc_idx = -1, ix = 0, iy = 0;
    for (int i = 0; i < s->win[0].nitems; i++)
        if (strcmp(window_items(&s->win[0])[i].name, "Documents") == 0) doc_idx = i;
    CHECK(doc_idx >= 0 && ospi_item_center(s, 0, doc_idx, &ix, &iy) == 0);
    session_tick(s, 5000);
    click_at(s, ix, iy, 1);
    session_tick(s, 5450);
    click_at(s, ix, iy, 2); /* Host says 2 clicks, but 450ms > 300ms -> single click! */
    CHECK(s->nwin == 1);

    session_command(s, CMD_CP_KEYBOARD);
    click_at(s, c.x + 158 + 28, c.y + 170, 1); /* Slow 750ms */
    click_at(s, c.x + 122 + 30, c.y + 214, 1); /* Wheel 1x */
    control_panels_close();

    session_tick(s, 7000);
    click_at(s, ix, iy, 1);
    session_tick(s, 7600);
    click_at(s, ix, iy, 1); /* 600ms <= 750ms -> opens Documents! */
    CHECK(s->nwin == 2);

    /* Keyboard & Mouse: Wheel speed (1x vs 3x) + Natural scrolling. */
    Window *dw = &s->win[s->nwin - 1];
    session_clamp(s);
    CHECK(window_max_scroll(dw) > 80);
    session_move(s, dw->x + 80, dw->y + 80);
    session_wheel(s, -1);
    CHECK(dw->scroll == 9);
    dw->scroll = 0;
    session_command(s, CMD_CP_KEYBOARD);
    click_at(s, c.x + 122 + 2 * 76 + 30, c.y + 214, 1); /* 3x */
    control_panels_close();
    session_wheel(s, -1);
    CHECK(dw->scroll == 30);
    session_command(s, CMD_CP_KEYBOARD);
    click_at(s, c.x + 40, c.y + 258, 1); /* Natural scroll */
    control_panels_close();
    dw->scroll = 50;
    session_wheel(s, -1);
    CHECK(dw->scroll == 20);

    /* Energy Saver: Idle dimming, 68k guest wake lock, and wake-on-network. */
    session_command(s, CMD_CP_ENERGY);
    click_at(s, c.x + 138 + 30, c.y + 74, 1); /* 5 min */
    click_at(s, c.x + 40, c.y + 330, 1); /* Wake for Network Transfers ON */
    CHECK(s->energy_sleep_mins == 5 && s->energy_prevent_guest_sleep == 1 && s->energy_wake_on_net == 1);
    control_panels_close();
    session_tick(s, 10000);
    session_move(s, 200, 200);
    s->guest_running = 1;
    session_tick(s, 10000 + 6 * 60000u);
    CHECK(s->display_dimmed == 0); /* Kept awake while 68k guest runs */
    s->guest_running = 0;
    session_tick(s, 10000 + 12 * 60000u);
    CHECK(s->display_dimmed == 1); /* Dims after 5 min idle */
    session_note_network_activity(s);
    CHECK(s->display_dimmed == 0); /* Wakes on network transfer */

    /* Memory: LRU Disk Cache, mounted RAM Disk, quota enforcement, clear-on-quit. */
    session_command(s, CMD_CP_MEMORY);
    click_at(s, c.x + 250, c.y + 172, 1); /* + -> 1024 KB custom cache */
    click_at(s, c.x + 40, c.y + 268, 1); /* Clear RAM Disk on Shutdown ON */
    click_at(s, c.x + 306 + 30, c.y + 304, 1); /* Enable 4 MB RAM Disk */
    CHECK(s->disk_cache_kb == 1024 && s->ram_disk_mb == 4 && s->ram_disk_clear_on_quit == 1);
    control_panels_close();

    char rbuf[64];
    size_t used0 = 0, cap0 = 0; unsigned hits0 = 0, hits1 = 0;
    volume_disk_cache_stats(&used0, &cap0, &hits0);
    CHECK(cap0 == 1024u * 1024u);
    CHECK(volume_read(root, "Documents/item_00.txt", rbuf, sizeof rbuf) == 0);
    CHECK(strcmp(rbuf, "doc 0\n") == 0);
    CHECK(volume_read(root, "Documents/item_00.txt", rbuf, sizeof rbuf) == 0);
    volume_disk_cache_stats(&used0, &cap0, &hits1);
    CHECK(hits1 >= hits0 + 1 && used0 > 0);
    {
        char p0[OSPI_PATH + 64];
        snprintf(p0, sizeof p0, "%s/Documents/item_00.txt", root);
        FILE *f0 = fopen(p0, "wb");
        if (f0) { fputs("updated after cache\n", f0); fclose(f0); }
    }
    CHECK(volume_read(root, "Documents/item_00.txt", rbuf, sizeof rbuf) == 0);
    CHECK(strcmp(rbuf, "updated after cache\n") == 0);

    Item root_items[OSPI_MAX_ITEMS];
    int nroot = volume_list(root, "", root_items, OSPI_MAX_ITEMS), found_rd = 0;
    for (int i = 0; i < nroot; i++) {
        if (strcmp(root_items[i].name, "RAM Disk") == 0) {
            found_rd = 1;
            CHECK(strcmp(desktop_asset_name(&root_items[i], ""), "hard_disk") == 0);
        }
    }
    CHECK(found_rd == 1);

    char big_src[OSPI_PATH + 64], copied[OSPI_NAME];
    snprintf(big_src, sizeof big_src, "%s/Documents/chunk3mb.bin", root);
    FILE *bf = fopen(big_src, "wb");
    CHECK(bf != NULL);
    if (bf) {
        char z[4096] = {0};
        for (int i = 0; i < 768; i++) fwrite(z, 1, sizeof z, bf); /* 3 MiB */
        fclose(bf);
    }
    CHECK(finder_copy_to(root, "Documents", "chunk3mb.bin", "RAM Disk", copied) == 0);
    CHECK(finder_copy_to(root, "Documents", "chunk3mb.bin", "RAM Disk", copied) == -1); /* > 4 MB quota! */

    /* TCP/IP & Network: Offline blocks net_download, install_garden, and CMD_BROWSER. */
    session_command(s, CMD_CP_NETWORK);
    click_at(s, c.x + 166 + 2 * 136 + 64, c.y + 36, 1); /* Offline */
    click_at(s, c.x + 40, c.y + 192, 1); /* Toggle Isolate MacTCP Guests -> OFF */
    click_at(s, c.x + 40, c.y + 224, 1); /* Toggle Allow Redirects -> OFF */
    control_panels_close();

    Os999NetConfig ncfg;
    net_get_config(&ncfg);
    CHECK(ncfg.connect_via == 2 && ncfg.https_only == 1 && ncfg.allow_redirects == 0 && ncfg.guest_isolated == 0);
    CHECK(getenv("OS999_MACTCP_ISOLATED") && strcmp(getenv("OS999_MACTCP_ISOLATED"), "0") == 0);

    char dl_dst[OSPI_PATH + 64], g_rel[OSPI_PATH], g_err[128];
    snprintf(dl_dst, sizeof dl_dst, "%s/Documents/never.bin", root);
    NetPolicy pol = {.max_bytes = 1024, .timeout_ms = 1000, .allow_url = NULL};
    NetResult nr = net_download("https://example.com/file.bin", dl_dst, &pol);
    CHECK(nr.status == NET_POLICY);
    CHECK(install_garden(root, "https://macintoshgarden.org/apps/test", g_rel, sizeof g_rel, g_err, sizeof g_err) == -1);
    CHECK(strstr(g_err, "Offline") != NULL);

    s->browser_request = 0;
    session_command(s, CMD_BROWSER);
    CHECK(s->browser_request == 0 && s->dialog != 0 && strcmp(s->dialog_title, "Network Offline") == 0);
    session_key(s, 27, 0);

    /* Restore default network policy and verify RAM Disk Clear-on-Shutdown on session_destroy. */
    session_command(s, CMD_CP_NETWORK);
    click_at(s, c.x + 420, c.y + 304, 1); /* Restore Defaults */
    net_get_config(&ncfg);
    CHECK(ncfg.connect_via == 0 && ncfg.allow_redirects == 1 && ncfg.guest_isolated == 1);

    session_destroy(s);
    uint64_t rd_after = 999; int rd_q = 0;
    CHECK(volume_ram_disk_usage(root, &rd_after, &rd_q) == 0 && rd_after == 0);

    os999_cdev_unregister_all();
    volume_disk_cache_set_kb(512);
    rm_tree(root);
}

int main(int argc, char **argv) {
    const char *icons_path = argc > 1 ? argv[1] : "assets/icons/ospi-icons.icl8";
    CHECK(desktop_assets_load(icons_path));
    test_general_appearance_sound_monitors();
    test_keyboard_energy_memory_network();
    desktop_assets_reset();
    printf("controlpanels_fx: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
