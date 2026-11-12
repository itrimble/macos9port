#include "os999_cdev.h"
#include "cdev_ui.h"
#include "desktop_theme.h"
#include "guest_memory.h"
#include "session_int.h"
#include "volume_capacity.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int custom_cache;
    int disk_cache_kb;
    int guest_ram_mb;
    int heap_guard;
    int clear_on_quit;
    int ram_disk_enabled;
    int ram_disk_mb;
} MemoryState;

static int g_ram_disk_error;
static MemoryState g_mem = {0, 512, 8, 1, 0, 0, 8};

static const int k_cache_steps[6] = {256, 512, 1024, 2048, 4096, 8192};
static const int k_guest_mb[4] = {2, 4, 8, 12};
static const char *const k_guest_labels[4] = {"2 MB", "4 MB", "8 MB", "12 MB"};
static const int k_ramdisk_mb[4] = {4, 8, 16, 32};
static const char *const k_ramdisk_labels[4] = {"4 MB", "8 MB", "16 MB", "32 MB"};

static int cache_step_idx(int kb) {
    for (int i = 0; i < 6; i++)
        if (kb <= k_cache_steps[i]) return i;
    return 5;
}

static void sync_session(const MemoryState *st, const os999_cdev_host_t *host, int sync_fs) {
    int active_kb = st->custom_cache ? st->disk_cache_kb : 512;
    volume_disk_cache_set_kb(active_kb);
    char mb_buf[16];
    snprintf(mb_buf, sizeof mb_buf, "%d", st->guest_ram_mb);
    setenv("OS999_GUEST_HEAP_MB", mb_buf, 1);
    setenv("OS999_GUEST_GUARD", st->heap_guard ? "1" : "0", 1);
    if (!host || !host->session) return;
    Session *s = host->session;
    s->disk_cache_kb = active_kb;
    s->guest_ram_mb = st->guest_ram_mb;
    s->guest_guard_pages = !!st->heap_guard;
    s->ram_disk_clear_on_quit = !!st->clear_on_quit;
    s->ram_disk_mb = st->ram_disk_enabled ? st->ram_disk_mb : 0;
    if (s->root[0] && (sync_fs || st->ram_disk_enabled)) {
        g_ram_disk_error = volume_ram_disk_sync(s->root, st->ram_disk_enabled, st->ram_disk_mb, 0);
        if (g_ram_disk_error) s->ram_disk_mb = 0;
        if (sync_fs) reload_folders(s);
    }
}

static int memory_load(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (host && host->store) {
        if (host->get_bool) {
            g_mem.custom_cache = host->get_bool(host->store, "memory", "custom_cache", 0);
            g_mem.heap_guard = host->get_bool(host->store, "memory", "heap_guard", 1);
            g_mem.clear_on_quit = host->get_bool(host->store, "memory", "clear_on_quit", 0);
            g_mem.ram_disk_enabled = host->get_bool(host->store, "memory", "ram_disk_enabled", 0);
        }
        if (host->get_int) {
            int kb = host->get_int(host->store, "memory", "disk_cache_kb", 512);
            g_mem.disk_cache_kb = k_cache_steps[cache_step_idx(kb)];
            int gm = host->get_int(host->store, "memory", "guest_ram_mb", 8);
            g_mem.guest_ram_mb = (gm == 2 || gm == 4 || gm == 8 || gm == 12) ? gm : 8;
            int rm = host->get_int(host->store, "memory", "ram_disk_mb", 8);
            g_mem.ram_disk_mb = (rm == 4 || rm == 8 || rm == 16 || rm == 32) ? rm : 8;
        }
    }
    if (host && host->session && host->store && host->get_int) { /* RAM display: remember the saved guest machine RAM / addressing */
        host->session->guest_machine_mb = host->get_int(host->store, "memory", "guest_machine_mb", 0);
        host->session->guest_addr_bits = host->get_int(host->store, "memory", "addr_bits", 0);
    }
    sync_session(&g_mem, host, 0);
    return 0;
}

static int memory_init(os999_cdev_t *self, const os999_cdev_host_t *host) {
    self->priv = &g_mem;
    g_ram_disk_error = 0;
    g_mem = (MemoryState){0, 512, 8, 1, 0, 0, 8};
    return memory_load(self, host);
}

static void memory_shutdown(os999_cdev_t *self) { self->priv = NULL; }

static int memory_save(os999_cdev_t *self, const os999_cdev_host_t *host) {
    (void)self;
    if (!host || !host->store || !host->flush_store) return -1;
    if (host->set_bool) {
        host->set_bool(host->store, "memory", "custom_cache", g_mem.custom_cache);
        host->set_bool(host->store, "memory", "heap_guard", g_mem.heap_guard);
        host->set_bool(host->store, "memory", "clear_on_quit", g_mem.clear_on_quit);
        host->set_bool(host->store, "memory", "ram_disk_enabled", g_mem.ram_disk_enabled);
    }
    if (host->set_int) {
        host->set_int(host->store, "memory", "disk_cache_kb", g_mem.disk_cache_kb);
        host->set_int(host->store, "memory", "guest_ram_mb", g_mem.guest_ram_mb);
        host->set_int(host->store, "memory", "ram_disk_mb", g_mem.ram_disk_mb);
    }
    return host->flush_store(host->store);
}

static Rect cache_def_btn(Rect c) { return (Rect){c.x + 14, c.y + 124, 126, 24}; }
static Rect cache_cus_btn(Rect c) { return (Rect){c.x + 148, c.y + 124, 126, 24}; }
static Rect cache_minus_btn(Rect c) { return (Rect){c.x + 14, c.y + 160, 36, 24}; }
static Rect cache_plus_btn(Rect c) { return (Rect){c.x + 238, c.y + 160, 36, 24}; }
static Rect guest_btn(Rect c, int i) { return (Rect){c.x + 314 + i * 66, c.y + 124, 60, 24}; }
static Rect guard_chk(Rect c) { return (Rect){c.x + 314, c.y + 162, 260, 24}; }
static Rect clear_chk(Rect c) { return (Rect){c.x + 16, c.y + 256, 268, 24}; }
static Rect ramdisk_chk(Rect c) { return (Rect){c.x + 306, c.y + 256, 256, 24}; }
/* Internal disk size: -1 GB, +1 GB, +10 GB along the right edge of the overview group. */
static Rect disk_btn(Rect c, int i) { return (Rect){c.x + c.w - 184 + i * 61, c.y + 73, 57, 18}; }
static const char *const k_disk_labels[3] = {"-1 GB", "+1 GB", "+10 GB"};
static const int k_disk_step_gb[3] = {-1, 1, 10};

static void disk_resize(const os999_cdev_host_t *host, int step_gb) {
    Session *s = host ? host->session : NULL;
    if (!s || !s->root[0]) return;
    VolumeSpace sp;
    char msg[200];
    if (volume_space(s->root, 1, &sp) != 0) { dialog_info(s, VOLUME_DISK_NAME, "The disk could not be measured."); return; }
    uint64_t step = (uint64_t)(step_gb < 0 ? -step_gb : step_gb) * VOLUME_CAPACITY_STEP_BYTES;
    if (step_gb < 0 && step >= sp.capacity) { dialog_info(s, VOLUME_DISK_NAME, "The disk cannot be made that small."); return; }
    uint64_t want = step_gb < 0 ? sp.capacity - step : sp.capacity + step;
    if (volume_capacity_set(s->root, want, msg, sizeof msg) != 0) { dialog_info(s, VOLUME_DISK_NAME, msg); return; }
    if (volume_space(s->root, 1, &sp) == 0) { s->free_bytes = sp.free; s->disk_capacity = sp.capacity; s->disk_used = sp.used; }
}

static Rect ramdisk_btn(Rect c, int i) { return (Rect){c.x + 306 + i * 66, c.y + 292, 60, 24}; }

static void apply_and_save(os999_cdev_t *self, const os999_cdev_host_t *host, int sync_fs) {
    sync_session(&g_mem, host, sync_fs);
    memory_save(self, host);
}

static int memory_event(os999_cdev_t *self, const os999_cdev_host_t *host,
                        const os999_cdev_event_t *ev, Rect c) {
    if (!ev) return 0;
    if (ev->type == OS999_CDEV_EV_KEY && !(ev->mods & MOD_CMD)) {
        if (ev->key >= '1' && ev->key <= '4') {
            g_mem.guest_ram_mb = k_guest_mb[ev->key - '1'];
            apply_and_save(self, host, 0);
            return 1;
        }
        return 0;
    }
    if (ev->type != OS999_CDEV_EV_DOWN) return 0;
    int x = ev->x, y = ev->y;
    for (int i = 0; i < 3; i++)
        if (rect_has(disk_btn(c, i), x, y)) { disk_resize(host, k_disk_step_gb[i]); return 1; }
    if (rect_has(cache_def_btn(c), x, y)) {
        g_mem.custom_cache = 0;
        g_mem.disk_cache_kb = 512;
        apply_and_save(self, host, 0);
        return 1;
    }
    if (rect_has(cache_cus_btn(c), x, y)) {
        g_mem.custom_cache = 1;
        apply_and_save(self, host, 0);
        return 1;
    }
    if (rect_has(cache_minus_btn(c), x, y)) {
        g_mem.custom_cache = 1;
        int idx = cache_step_idx(g_mem.disk_cache_kb);
        if (idx > 0) g_mem.disk_cache_kb = k_cache_steps[idx - 1];
        apply_and_save(self, host, 0);
        return 1;
    }
    if (rect_has(cache_plus_btn(c), x, y)) {
        g_mem.custom_cache = 1;
        int idx = cache_step_idx(g_mem.disk_cache_kb);
        if (idx < 5) g_mem.disk_cache_kb = k_cache_steps[idx + 1];
        apply_and_save(self, host, 0);
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (rect_has(guest_btn(c, i), x, y)) {
            g_mem.guest_ram_mb = k_guest_mb[i];
            apply_and_save(self, host, 0);
            return 1;
        }
        if (rect_has(ramdisk_btn(c, i), x, y)) {
            g_mem.ram_disk_enabled = 1;
            g_mem.ram_disk_mb = k_ramdisk_mb[i];
            apply_and_save(self, host, 1);
            return 1;
        }
    }
    if (rect_has(guard_chk(c), x, y)) {
        g_mem.heap_guard = !g_mem.heap_guard;
        apply_and_save(self, host, 0);
        return 1;
    }
    if (rect_has(clear_chk(c), x, y)) {
        g_mem.clear_on_quit = !g_mem.clear_on_quit;
        apply_and_save(self, host, 0);
        return 1;
    }
    if (rect_has(ramdisk_chk(c), x, y)) {
        g_mem.ram_disk_enabled = !g_mem.ram_disk_enabled;
        apply_and_save(self, host, 1);
        return 1;
    }
    return 0;
}

static void fmt_bytes(char *out, size_t n, uint64_t b) {
    double gb = (double)b / (1024.0 * 1024.0 * 1024.0);
    if (gb >= 1.0) snprintf(out, n, "%.1f GB", gb);
    else snprintf(out, n, "%llu MB", (unsigned long long)(b / (1024ull * 1024ull)));
}

static void memory_render(os999_cdev_t *self, const os999_cdev_host_t *host,
                          Fb *fb, Rect c) {
    (void)self;
    uint32_t fg = desktop_theme_color(THEME_FOREGROUND);
    const Session *s = host ? host->session : NULL;
    uint64_t ram = (s && s->ram > 0) ? s->ram : host_ram();
    VolumeSpace disk = {0};
    int have_disk = s && s->root[0] && volume_space(s->root, 0, &disk) == 0;
    uint64_t disk_free = have_disk ? disk.free : (s ? s->free_bytes : 0);

    cdev_ui_group(fb, (Rect){c.x, c.y + 2, c.w, 92}, "Host & Runtime Memory Overview");
    cdev_ui_icon(fb, c.x + 16, c.y + 26, "cpu");
    char ram_s[32], free_s[32], info[128];
    fmt_bytes(ram_s, sizeof ram_s, ram);
    fmt_bytes(free_s, sizeof free_s, disk_free);
    snprintf(info, sizeof info, "Host RAM: %s (%s)   |   Disk Free: %s",
             ram_s, (s && s->arch[0]) ? s->arch : host_arch(), free_s);
    fb_text(fb, c.x + 62, c.y + 24, fg, info);
    if (have_disk) {
        char used_s[32], cap_s[32], line[96];
        volume_format_bytes(used_s, sizeof used_s, disk.used);
        volume_format_bytes(cap_s, sizeof cap_s, disk.capacity);
        snprintf(line, sizeof line, "%s: %s used of %s", VOLUME_DISK_NAME, used_s, cap_s);
        fb_text(fb, c.x + 62, c.y + 76, desktop_theme_color(THEME_DISABLED), line);
        for (int i = 0; i < 3; i++) cdev_ui_button(fb, disk_btn(c, i), k_disk_labels[i], 0, 1);
    }

    Rect bar = {c.x + 62, c.y + 52, c.w - 78, 20};
    fb_fill(fb, bar.x, bar.y, bar.w, bar.h, desktop_theme_color(THEME_TRACK));
    int w_sys = 96;
    int w_guest = g_mem.guest_ram_mb * 10;
    int w_rd = g_mem.ram_disk_enabled ? g_mem.ram_disk_mb * 4 : 0;
    fb_fill(fb, bar.x + 1, bar.y + 1, w_sys, bar.h - 2, cdev_ui_selection_color());
    fb_fill(fb, bar.x + 1 + w_sys, bar.y + 1, w_guest, bar.h - 2, 0xFF2E8B57u);
    if (w_rd > 0)
        fb_fill(fb, bar.x + 1 + w_sys + w_guest, bar.y + 1, w_rd, bar.h - 2, 0xFFD08C28u);
    fb_frame(fb, bar.x, bar.y, bar.w, bar.h, desktop_theme_color(THEME_FRAME));

    cdev_ui_group(fb, (Rect){c.x, c.y + 102, 288, 122}, "Disk Cache");
    cdev_ui_button(fb, cache_def_btn(c), "Default (512K)", !g_mem.custom_cache, 1);
    cdev_ui_button(fb, cache_cus_btn(c), "Custom Size", g_mem.custom_cache, 1);
    cdev_ui_button(fb, cache_minus_btn(c), "-", 0, g_mem.disk_cache_kb > 256);
    Rect cbox = {c.x + 58, c.y + 160, 172, 24};
    fb_fill(fb, cbox.x, cbox.y, cbox.w, cbox.h, desktop_theme_color(THEME_PANEL));
    fb_frame(fb, cbox.x, cbox.y, cbox.w, cbox.h, desktop_theme_color(THEME_FRAME));
    char clbl[32], cstat[48];
    int active_kb = g_mem.custom_cache ? g_mem.disk_cache_kb : 512;
    size_t dc_used = 0; unsigned dc_hits = 0;
    volume_disk_cache_stats(&dc_used, NULL, &dc_hits);
    snprintf(clbl, sizeof clbl, "%d KB Cache", active_kb);
    fb_text(fb, cbox.x + (cbox.w - fb_text_width(clbl)) / 2, cbox.y + 5, fg, clbl);
    cdev_ui_button(fb, cache_plus_btn(c), "+", 0, g_mem.disk_cache_kb < 8192);
    snprintf(cstat, sizeof cstat, "Cached: %zu KB (%u hits)", (dc_used + 1023) / 1024, dc_hits);
    fb_text(fb, c.x + 14, c.y + 196, desktop_theme_color(THEME_DISABLED), cstat);

    cdev_ui_group(fb, (Rect){c.x + 300, c.y + 102, c.w - 300, 122}, "68k Runtime Heap");
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, guest_btn(c, i), k_guest_labels[i],
                       g_mem.guest_ram_mb == k_guest_mb[i], 1);
    cdev_ui_checkbox(fb, guard_chk(c), "Software Bounds & Redzones", g_mem.heap_guard);
    GuestMemoryConfig gm = guest_memory_from_env(s ? s->guest_machine_mb : 0, s ? s->guest_addr_bits : 0);
    char gml[64];
    snprintf(gml, sizeof gml, "Guest RAM %u MB, %d-bit addressing.", gm.ram_mb, gm.addr32 ? 32 : 24);
    fb_text(fb, c.x + 314, c.y + 196, desktop_theme_color(THEME_DISABLED), gml);

    cdev_ui_group(fb, (Rect){c.x, c.y + 232, c.w, 124}, "Scratch Volume & Shutdown Policy");
    cdev_ui_checkbox(fb, clear_chk(c), "Clear RAM Disk on Shutdown", g_mem.clear_on_quit);
    fb_text(fb, c.x + 16, c.y + 290, desktop_theme_color(THEME_DISABLED),
            g_mem.clear_on_quit ? "Policy: Wipes scratch files on quit" : "Policy: Keeps RAM Disk files");
    fb_text(fb, c.x + 16, c.y + 314, desktop_theme_color(THEME_DISABLED),
            "Host-backed scratch directory.");

    cdev_ui_checkbox(fb, ramdisk_chk(c), "Enable Scratch RAM Disk", g_mem.ram_disk_enabled);
    for (int i = 0; i < 4; i++)
        cdev_ui_button(fb, ramdisk_btn(c, i), k_ramdisk_labels[i],
                       g_mem.ram_disk_enabled && g_mem.ram_disk_mb == k_ramdisk_mb[i], 1);
    uint64_t rd_used = 0; int rd_q = 0;
    if (s && s->root[0] && g_mem.ram_disk_enabled)
        (void)volume_ram_disk_usage(s->root, &rd_used, &rd_q);
    char rdl[64];
    if (g_ram_disk_error)
        snprintf(rdl, sizeof rdl, "Unavailable: existing data preserved");
    else if (g_mem.ram_disk_enabled)
        snprintf(rdl, sizeof rdl, "Scratch: %llu KB used / %d MB quota",
                 (unsigned long long)((rd_used + 1023ull) / 1024ull), g_mem.ram_disk_mb);
    else
        snprintf(rdl, sizeof rdl, "Disabled (host-backed scratch storage)");
    fb_text(fb, c.x + 306, c.y + 324, desktop_theme_color(THEME_DISABLED), rdl);
}

static const os999_cdev_t k_memory_cdev = {
    .api_version = OS999_CDEV_API_VERSION,
    .id = "memory",
    .title = "Memory",
    .summary = "Disk cache, 68k heap, and scratch RAM disk",
    .icon_name = "cpu",
    .category = OS999_CDEV_CAT_SYSTEM,
    .init = memory_init,
    .shutdown = memory_shutdown,
    .load_state = memory_load,
    .save_state = memory_save,
    .handle_event = memory_event,
    .render = memory_render,
    .priv = NULL
};

const os999_cdev_t *os999_cdev_memory_module(void) { return &k_memory_cdev; }
