/* Render the phone shell on every device profile and posture, headless.
 *
 *   build/mobile/mobile_shots OUT_DIR
 *
 * Writes OUT_DIR/<device>-<posture>.png (the screen at half size),
 * OUT_DIR/frame-<device>-<posture>.png (in a simple device frame) and
 * OUT_DIR/sheet-<device>.png (all postures side by side), plus guide.png.
 * Run from the repository root so fonts, icons and wallpapers resolve.
 */
#include "mobile_shell.h"
#include "kbd_gfx.h"
#include "mobile_png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t now = 1000;

static void frames(MobileShell *m, int n) {
    for (int i = 0; i < n; i++) mobile_shell_frame(m, now += 16);
    m->dirty = 1;
    mobile_shell_frame(m, now += 16);
}

static void tap(MobileShell *m, float x, float y) {
    mobile_shell_finger(m, MSH_DOWN, 9, x, y, now);
    mobile_shell_finger(m, MSH_UP, 9, x, y, now + 50);
    now += 120;
    mobile_shell_frame(m, now);
}

static void tap_desk(MobileShell *m, int dx, int dy) {
    float sx, sy;
    mobile_view_to_screen(&m->view, &m->layout, dx + 0.5f, dy + 0.5f, &sx, &sy);
    tap(m, sx, sy);
}

static void tap_tile(MobileShell *m, int tile) {
    Rect r = tile == MS_TAB ? m->strip.tab : m->strip.tiles[tile];
    tap(m, r.x + r.w / 2.0f, r.y + r.h / 2.0f);
}

static const char *posture_id(int p) {
    static const char *ids[] = {"cover", "unfolded", "laptop", "book"};
    return ids[p];
}

/* Screen inside a plain device body, for sharing and for reviewing layouts. */
static uint32_t *framed(const MobileShell *m, int posture, int *fw, int *fh) {
    float dp = m->layout.dp;
    int bez = (int)(14 * dp), pad = (int)(36 * dp);
    int w = m->canvas.w + 2 * (bez + pad), h = m->canvas.h + 2 * (bez + pad);
    uint32_t *px = malloc((size_t)w * h * sizeof *px);
    if (!px) return NULL;
    Fb fb;
    fb_init(&fb, px, w, h);
    g_rrect(&fb, 0, 0, w, h, 0, 0xFFF4F4F4u, 0xFFE2E2E8u, 1);
    g_shadow(&fb, pad, pad + 10 * dp, w - 2 * pad, h - 2 * pad, 34 * dp, 18 * dp, MUI_INK, 0.35f);
    g_rrect(&fb, pad, pad, w - 2 * pad, h - 2 * pad, 34 * dp, 0xFF3A3B3Fu, 0xFF1E1F22u, 1);
    int ox = pad + bez, oy = pad + bez;
    for (int y = 0; y < m->canvas.h; y++)
        memcpy(px + (size_t)(oy + y) * w + ox, m->canvas.px + (size_t)y * m->canvas.w,
               (size_t)m->canvas.w * sizeof *px);
    const MobileScreen *s = &m->screen;
    if (posture != MP_COVER && s->fold_axis != MF_AXIS_NONE) {   /* the crease catches the light */
        if (s->fold_axis == MF_AXIS_VERTICAL)
            for (int i = -3; i <= 3; i++)
                g_line(&fb, ox + s->fold.x + i * dp, oy, ox + s->fold.x + i * dp, oy + s->h, dp,
                       i < 0 ? MUI_INK : MUI_WHITE, 0.05f);
        else
            for (int i = -3; i <= 3; i++)
                g_line(&fb, ox, oy + s->fold.y + i * dp, ox + s->w, oy + s->fold.y + i * dp, dp,
                       i < 0 ? MUI_INK : MUI_WHITE, 0.05f);
    }
    *fw = w;
    *fh = h;
    return px;
}

typedef struct { uint32_t *px; int w, h; } Img;

static void save_sheet(const char *path, Img *imgs, int n) {
    int target = 1000, gap = 40, W = gap, H = target + 2 * gap;
    Img small[MP_POSTURE_COUNT];
    for (int i = 0; i < n; i++) {
        int k = (imgs[i].h + target - 1) / target;
        small[i].px = mobile_downscale(imgs[i].px, imgs[i].w, imgs[i].h, k, &small[i].w, &small[i].h);
        W += small[i].w + gap;
    }
    uint32_t *px = malloc((size_t)W * H * sizeof *px);
    if (!px) return;
    for (size_t i = 0; i < (size_t)W * H; i++) px[i] = 0xFFE9E9EEu;
    int x = gap;
    for (int i = 0; i < n; i++) {
        int y = gap + (target - small[i].h) / 2;
        for (int r = 0; r < small[i].h && small[i].px; r++)
            memcpy(px + (size_t)(y + r) * W + x, small[i].px + (size_t)r * small[i].w,
                   (size_t)small[i].w * sizeof *px);
        x += small[i].w + gap;
        free(small[i].px);
    }
    mobile_png_write(path, px, W, H, 0);
    free(px);
}

static void write_half(const char *path, const uint32_t *px, int w, int h) {
    int ow, oh;
    uint32_t *s = mobile_downscale(px, w, h, 2, &ow, &oh);
    if (s) mobile_png_write(path, s, ow, oh, 0);
    free(s);
}

static void reset(MobileShell *m) {
    mobile_shell_finger(m, MSH_CANCEL, 0, 0, 0, now);
    kbd_show(m->kbd, 0);
    m->strip.expanded = 0;
    m->toast.active = 0;
    if (m->touch.mode != MT_DIRECT) mobile_touch_set_mode(&m->touch, MT_DIRECT);
    m->pointer_visible = 0;
    mobile_view_reset(&m->view);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "mobile/assets/shots";
    char parent[512];
    snprintf(parent, sizeof parent, "%s", out);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') { *q = 0; mkdir(parent, 0755); *q = '/'; }
    mkdir(out, 0755);
    char root[] = "/tmp/os999-mobile-shots-XXXXXX";
    if (!mkdtemp(root) || volume_seed(root) != 0) return 1;
    Session *s = session_create(root);
    if (!s) return 1;
    kbd_load_fonts("fonts/ChicagoFLF.ttf");
    MobileShell m;
    if (mobile_shell_init(&m, s, (MobileShellHost){0}, root) != 0) return 1;
    char path[512];
    int opened = 0;
    for (int d = 0; d < mobile_device_count(); d++) {
        const MobileDeviceProfile *dev = mobile_device_at(d);
        if (dev->tablet) continue;   /* tablets: mobile_tablets */
        Img imgs[MP_POSTURE_COUNT];
        int n = 0;
        for (int p = MP_COVER; p < MP_POSTURE_COUNT; p++) {
            if (dev->flip && p == MP_BOOK) continue;
            MobileScreen sc;
            mobile_device_screen(dev, (MobilePosture)p, &sc);
            mobile_shell_set_screen(&m, &sc, now);
            reset(&m);
            frames(&m, 4);
            if (d == 0 && p == MP_COVER) {   /* first launch */
                frames(&m, 2);
                snprintf(path, sizeof path, "%s/guide.png", out);
                write_half(path, m.canvas.px, m.canvas.w, m.canvas.h);
                Rect b = m.coach.button;
                tap(&m, b.x + b.w / 2.0f, b.y + b.h / 2.0f);
            }
            if (!opened) {   /* open the startup disk like a user would: double-tap */
                Rect icon, hit;
                desktop_icon_rect(s->sw, s->sh, 0, &icon, &hit);
                tap_desk(&m, hit.x + hit.w / 2, hit.y + hit.h / 2);
                now += 80;
                tap_desk(&m, hit.x + hit.w / 2, hit.y + hit.h / 2);
                opened = 1;
                frames(&m, 4);
            }
            if (p == MP_COVER) {
                tap_tile(&m, MS_TAB);
                frames(&m, 2);
                tap_tile(&m, MS_KEYBOARD);
                m.toast.active = 0;
                frames(&m, 50);
            } else if (p == MP_FLAT) {
                float sx, sy;   /* hold on the File menu: loupe and the menu ring */
                mobile_view_to_screen(&m.view, &m.layout, 60, 10, &sx, &sy);
                mobile_shell_finger(&m, MSH_DOWN, 4, sx, sy, now);
                now += 700;
                frames(&m, 1);
            } else if (p == MP_LAPTOP) {
                frames(&m, 30);   /* the posture balloon is up */
                m.pointer_visible = 1;
                m.touch.px = s->sw * 0.62f;
                m.touch.py = s->sh * 0.45f;
                frames(&m, 1);
            } else {
                tap_tile(&m, MS_TAB);
                frames(&m, 2);
                tap_tile(&m, MS_POINTER);
                m.toast.active = 0;
                frames(&m, 2);
            }
            snprintf(path, sizeof path, "%s/%s-%s.png", out, dev->id, posture_id(p));
            write_half(path, m.canvas.px, m.canvas.w, m.canvas.h);
            imgs[n].px = framed(&m, p, &imgs[n].w, &imgs[n].h);
            if (imgs[n].px) {
                snprintf(path, sizeof path, "%s/frame-%s-%s.png", out, dev->id, posture_id(p));
                write_half(path, imgs[n].px, imgs[n].w, imgs[n].h);
                n++;
            }
            printf("%-20s %-9s screen %4dx%-4d desktop %4dx%-4d at %dx\n", dev->id, posture_id(p),
                   sc.w, sc.h, m.layout.desk_w, m.layout.desk_h, m.layout.scale);
        }
        snprintf(path, sizeof path, "%s/sheet-%s.png", out, dev->id);
        save_sheet(path, imgs, n);
        for (int i = 0; i < n; i++) free(imgs[i].px);
    }
    mobile_shell_free(&m);
    session_destroy(s);
    return 0;
}
