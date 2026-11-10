#include "about_panel.h"

#include "finder_apps.h"
#include "os999_startup_sprite.h"
#include "text_system.h"

#include <stdio.h>
#include <string.h>
#ifdef __APPLE__
#include <mach/mach.h>
#else
#include <sys/resource.h>
#endif

uint64_t about_panel_resident_bytes(void) {
#ifdef __APPLE__
    mach_task_basic_info_data_t info; mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &n) != KERN_SUCCESS) return 0;
    return info.resident_size;
#else
    struct rusage ru; if (getrusage(RUSAGE_SELF, &ru)) return 0;
    return (uint64_t)ru.ru_maxrss * 1024;
#endif
}

static void mb_text(char *out, size_t n, uint64_t bytes) {
    double mb = (double)bytes / (1024.0 * 1024.0);
    if (mb >= 1024) snprintf(out, n, "%.1f GB", mb / 1024); else snprintf(out, n, "%.1f MB", mb);
}

/* Draw text scaled up with nearest-neighbour so the wordmark reads as a title. */
static void big_text(Fb *fb, int x, int y, int scale, uint32_t color, const char *text) {
    enum { MAXW = 200, MAXH = 20 };
    static uint32_t buf[MAXW * MAXH];
    int w = fb_text_width(text); if (w > MAXW) w = MAXW;
    for (int i = 0; i < MAXW * MAXH; i++) buf[i] = 0;
    Fb t = {.px = buf, .w = MAXW, .h = MAXH};
    fb_text(&t, 0, 0, color, text);
    for (int yy = 0; yy < MAXH; yy++) for (int xx = 0; xx < w; xx++)
        if (buf[yy * MAXW + xx] >> 24) fb_fill(fb, x + xx * scale, y + yy * scale, scale, scale, color);
}

static void bar(Fb *fb, int x, int y, int w, int h, double frac) {
    fb_fill(fb, x, y, w, h, C_WHITE);
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int f = (int)(frac * (w - 2)); if (frac > 0 && f < 2) f = 2;
    fb_fill(fb, x + 1, y + 1, f, h - 2, 0xff2f7fd8u);
    fb_frame(fb, x, y, w, h, C_BLACK);
}

static void app_row(Fb *fb, int x, int y, int w, const char *name, const char *size, double frac) {
    fb_text(fb, x, y, C_BLACK, name);
    fb_text(fb, x + 190, y, C_BLACK, size);
    if (frac >= 0) bar(fb, x + 270, y + 1, w - 270, 11, frac);
}

int about_panel_paint(Fb *fb, const Session *s, const Window *w, const Layout *lay) {
    if (!s || !w || strcmp(w->rel, ":about")) return 0;
    int x = lay->content.x + 8, y = lay->content.y + 6, cw = lay->content.w - 16;
    fb_fill(fb, lay->content.x, lay->content.y, lay->content.w, lay->content.h, C_MENU);
    /* Banner */
    fb_fill(fb, x, y, cw, 64, C_WHITE); fb_frame(fb, x, y, cw, 64, C_BLACK);
    os999_startup_draw_fruit(fb, x + 38, y + 34, 2, 0.0);
    big_text(fb, x + 84, y + 14, 3, C_BLACK, "OS999");
    y += 72;
    char ram[32], mem[32], part[32], line[96];
    mb_text(ram, sizeof ram, s->ram);
#ifdef OS999_BUILD_NUMBER
    snprintf(line, sizeof line, "Version:  OS999 " OS999_VERSION ", Build %d (%s)", OS999_BUILD_NUMBER, s->arch);
#else
    snprintf(line, sizeof line, "Version:  OS999 " OS999_VERSION " (%s)", s->arch);
#endif
    fb_text(fb, x, y, C_BLACK, line);
    snprintf(line, sizeof line, "Built-in Memory:  %s", ram);
    fb_text(fb, x, y + 16, C_BLACK, line);
    snprintf(line, sizeof line, "Application memory:  %d MB per program", s->guest_ram_mb ? s->guest_ram_mb : 8);
    fb_text(fb, x, y + 32, C_BLACK, line);
    snprintf(line, sizeof line, "Letters:  %s", typeface_label());
    fb_text(fb, x, y + 48, C_BLACK, line);
    y += 68;
    fb_fill(fb, x, y, cw, 1, C_BLACK);
    fb_text(fb, x, y + 4, C_DISABLED, "Running now (bars compare sizes)");
    y += 22;
    uint64_t me = about_panel_resident_bytes();
    double part_b = (s->guest_ram_mb ? s->guest_ram_mb : 8) * 1048576.0;
    double total = s->guest_active && part_b > (double)me ? part_b : (me ? (double)me : 1);
    mb_text(mem, sizeof mem, me);
    app_row(fb, x, y, cw, "OS999 Finder", mem, (double)me / total); y += 16;
    if (s->guest_active) {
        snprintf(part, sizeof part, "%d MB", s->guest_ram_mb ? s->guest_ram_mb : 8);
        app_row(fb, x, y, cw, s->guest_title[0] ? s->guest_title : "Classic app", part,
                (double)(s->guest_ram_mb ? s->guest_ram_mb : 8) * 1048576.0 / total); y += 16;
    }
    if (s->browser_active) { app_row(fb, x, y, cw, "Web Browser", "helper", -1); y += 16; }
    if (finder_apps_media_running(s)) { app_row(fb, x, y, cw, "Media", "running", -1); y += 16; }
    for (int i = 0; i < s->nwin && y < lay->content.y + lay->content.h - 90; i++)
        if (s->win[i].kind == WIN_MOVIE && s->win[i].movie) { app_row(fb, x, y, cw, "Movie Player", "playing", -1); y += 16; break; }
    text_draw(fb, x, y + 6, cw, C_BLACK, window_text(w));
    int ly = lay->content.y + lay->content.h - 30;      /* the link target matches desktop_info_click */
    fb_fill(fb, x, ly, cw, 22, C_MENU); fb_frame(fb, x, ly, cw, 22, C_BLACK);
    fb_text(fb, x + 6, ly + 3, 0xff154f9cu, "apps.remnantsecurity.com");
    return 1;
}
