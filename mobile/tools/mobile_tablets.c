/* OS999 on tablets with a hardware keyboard, rendered headless.
 *
 *   build/mobile/mobile_tablets OUT_DIR
 *
 * For each tablet profile: the screen with a keyboard and pointer attached
 * (<id>-keyboard.png). For the 13-inch iPad, three mockups with the keyboard
 * styles people actually use: a floating keyboard case with a trackpad
 * (case-trackpad.png), a fabric folio (folio.png) and a separate Bluetooth
 * keyboard and mouse (bluetooth.png), plus tablets-sheet.png. The keyboards
 * are drawn generically: no maker's design, logo or name on them.
 * Run from the repository root.
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
typedef struct { uint32_t *px; int w, h; } Img;
typedef enum { STYLE_CASE, STYLE_FOLIO, STYLE_BLUETOOTH } Style;

static void frames(MobileShell *m, int n) {
    for (int i = 0; i < n; i++) mobile_shell_frame(m, now += 16);
    m->dirty = 1;
    mobile_shell_frame(m, now += 16);
}

static void click_desk(MobileShell *m, int dx, int dy, int clicks) {
    float sx, sy;
    mobile_view_to_screen(&m->view, &m->layout, dx + 0.5f, dy + 0.5f, &sx, &sy);
    mobile_shell_mouse(m, MSH_MOVE, sx, sy, 0, 0, now);
    mobile_shell_mouse(m, MSH_DOWN, sx, sy, MSH_BUTTON_PRIMARY, clicks, now += 20);
    mobile_shell_mouse(m, MSH_UP, sx, sy, MSH_BUTTON_PRIMARY, clicks, now += 60);
    frames(m, 2);
}

static void hover_desk(MobileShell *m, int dx, int dy) {
    float sx, sy;
    mobile_view_to_screen(&m->view, &m->layout, dx + 0.5f, dy + 0.5f, &sx, &sy);
    mobile_shell_mouse(m, MSH_MOVE, sx, sy, 0, 0, now);
}

static Img copy_canvas(const MobileShell *m, int k) {
    Img out = {0};
    out.px = mobile_downscale(m->canvas.px, m->canvas.w, m->canvas.h, k, &out.w, &out.h);
    return out;
}

/* A keyboard deck in perspective: top edge (tx0..tx1, ty), bottom (bx0..bx1, by). */
static void deck(Fb *fb, float tx0, float tx1, float ty, float bx0, float bx1, float by,
                 uint32_t body, uint32_t key_hi, uint32_t key_lo, int trackpad, float u) {
    g_shadow(fb, bx0, by - 6 * u, bx1 - bx0, 10 * u, 6 * u, 14 * u, MUI_INK, 0.35f);
    g_rrect(fb, tx0 + 1, ty, tx1 - tx0 - 2, by - ty, 0, body, body, 1);
    g_triangle(fb, tx0 + 2, ty, tx0 + 2, by, bx0, by, body, 1);
    g_triangle(fb, tx1 - 2, ty, bx1, by, tx1 - 2, by, body, 1);
    float rows = 5, key_area = trackpad ? 0.62f : 0.86f;
    for (int r = 0; r < rows; r++) {
        float t0 = 0.08f + r * key_area / rows, t1 = t0 + key_area / rows * 0.82f;
        float y0 = ty + (by - ty) * t0, y1 = ty + (by - ty) * t1;
        float l = tx0 + (bx0 - tx0) * (t0 + t1) / 2 + 8 * u, rgt = tx1 + (bx1 - tx1) * (t0 + t1) / 2 - 8 * u;
        int n = r == 4 ? 7 : 14;
        float gap = 2.2f * u, kw = (rgt - l - (n - 1) * gap) / n;
        for (int k = 0; k < n; k++) {
            float w = kw;
            float x = l + k * (kw + gap);
            if (r == 4 && k == 3) w = kw * 1.0f;
            g_rrect(fb, x, y0, w, y1 - y0, 2.4f * u, key_hi, key_lo, 1);
        }
    }
    if (trackpad) {
        float t0 = 0.74f, t1 = 0.95f, y0 = ty + (by - ty) * t0, y1 = ty + (by - ty) * t1;
        float cx = (tx0 + tx1 + bx0 + bx1) / 4, w = (tx1 - tx0) * 0.38f;
        g_rrect(fb, cx - w / 2, y0, w, y1 - y0, 6 * u, g_shade(body, 1.18f), g_shade(body, 1.08f), 1);
        g_rrect_stroke(fb, cx - w / 2, y0, w, y1 - y0, 6 * u, 1.2f * u, g_shade(body, 0.75f), 1);
    }
}

static void blit(Fb *fb, const Img *src, int x, int y) {
    for (int r = 0; r < src->h; r++) {
        if (y + r < 0 || y + r >= fb->h) continue;
        memcpy(fb->px + (size_t)(y + r) * fb->w + x, src->px + (size_t)r * src->w,
               (size_t)src->w * sizeof *src->px);
    }
}

static Img mockup(const Img *screen, Style style) {
    float u = screen->w / 700.0f;   /* layout unit tied to the screen size */
    int bez = (int)(16 * u), pad = (int)(60 * u);
    int tw = screen->w + 2 * bez, th = screen->h + 2 * bez;
    int deck_h = (int)(tw * (style == STYLE_BLUETOOTH ? 0.30f : 0.40f));
    Img out = {0};
    out.w = tw + 2 * pad + (style == STYLE_BLUETOOTH ? (int)(120 * u) : 0);
    out.h = th + deck_h + 2 * pad + (int)(40 * u);
    out.px = malloc((size_t)out.w * out.h * sizeof *out.px);
    if (!out.px) return out;
    Fb fb = {.px = out.px, .w = out.w, .h = out.h};
    g_rrect(&fb, 0, 0, out.w, out.h, 0, 0xFFF4F4F6u, 0xFFDCDCE2u, 1);
    float tx = pad, ty = pad;
    float dtop = ty + th + (style == STYLE_CASE ? 26 * u : 4 * u);
    if (style == STYLE_CASE) {   /* the tablet floats on a cantilever above the keys */
        deck(&fb, tx + 10 * u, tx + tw - 10 * u, dtop, tx - 14 * u, tx + tw + 14 * u, dtop + deck_h,
             0xFF2A2B2Fu, 0xFF3D3E44u, 0xFF303136u, 1, u);
        g_rrect(&fb, tx + tw * 0.2f, ty + th - 30 * u, tw * 0.6f, dtop - (ty + th) + 40 * u, 8 * u,
                0xFF2E2F33u, 0xFF232427u, 1);
    } else if (style == STYLE_FOLIO) {   /* fabric folio: keys flush against the tablet's edge */
        deck(&fb, tx, tx + tw, dtop, tx - 22 * u, tx + tw + 22 * u, dtop + deck_h,
             0xFF3A4556u, 0xFF465265u, 0xFF3F4A5Bu, 0, u);
        g_rrect(&fb, tx, ty + th - 6 * u, tw, 14 * u, 4 * u, 0xFF323C4Bu, 0xFF2B3442u, 1);
    } else {   /* tablet on a stand, a separate Bluetooth keyboard and a mouse */
        g_triangle(&fb, tx + tw * 0.35f, ty + th - 10 * u, tx + tw * 0.65f, ty + th - 10 * u,
                   tx + tw * 0.70f, ty + th + 26 * u, 0xFFB9BAC0u, 1);
        g_triangle(&fb, tx + tw * 0.35f, ty + th - 10 * u, tx + tw * 0.70f, ty + th + 26 * u,
                   tx + tw * 0.30f, ty + th + 26 * u, 0xFFB9BAC0u, 1);
        float ky = ty + th + 60 * u;
        deck(&fb, tx + 40 * u, tx + tw - 40 * u, ky, tx + 20 * u, tx + tw - 20 * u, ky + deck_h - 40 * u,
             0xFFE4E4E8u, 0xFFFFFFFFu, 0xFFEDEDF0u, 0, u);
        float mx = tx + tw + 30 * u, my = ky + deck_h * 0.25f;   /* mouse */
        g_shadow(&fb, mx, my + 6 * u, 70 * u, 110 * u, 35 * u, 10 * u, MUI_INK, 0.3f);
        g_rrect(&fb, mx, my, 70 * u, 110 * u, 35 * u, 0xFFFFFFFFu, 0xFFE2E2E6u, 1);
        g_line(&fb, mx + 35 * u, my + 4 * u, mx + 35 * u, my + 38 * u, 1.2f * u, 0xFFBDBDC4u, 1);
    }
    g_shadow(&fb, tx, ty + 10 * u, tw, th, 26 * u, 18 * u, MUI_INK, 0.4f);
    g_rrect(&fb, tx, ty, tw, th, 26 * u, 0xFF2C2D31u, 0xFF1A1B1Eu, 1);
    blit(&fb, screen, (int)tx + bez, (int)ty + bez);
    g_disc(&fb, tx + tw / 2.0f, ty + bez / 2.0f, 2.5f * u, 0xFF0C0C0Eu, 1);   /* front camera */
    return out;
}

static void save(const char *dir, const char *name, Img img, int k) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int w, h;
    uint32_t *s = k > 1 ? mobile_downscale(img.px, img.w, img.h, k, &w, &h) : NULL;
    mobile_png_write(path, s ? s : img.px, s ? w : img.w, s ? h : img.h, 0);
    free(s);
    printf("wrote %s\n", path);
}

static void sheet(const char *dir, Img *imgs, int n) {
    int target = 900, gap = 40, W = gap, H = target + 2 * gap;
    Img small[3];
    for (int i = 0; i < n; i++) {
        int k = (imgs[i].h + target - 1) / target;
        small[i].px = mobile_downscale(imgs[i].px, imgs[i].w, imgs[i].h, k, &small[i].w, &small[i].h);
        W += small[i].w + gap;
    }
    Img all = {malloc((size_t)W * H * sizeof(uint32_t)), W, H};
    if (!all.px) return;
    for (int i = 0; i < W * H; i++) all.px[i] = 0xFFE9E9EEu;
    Fb fb = {.px = all.px, .w = W, .h = H};
    int x = gap;
    for (int i = 0; i < n; i++) {
        if (small[i].px) blit(&fb, &small[i], x, gap + (target - small[i].h) / 2);
        x += small[i].w + gap;
        free(small[i].px);
    }
    save(dir, "tablets-sheet.png", all, 1);
    free(all.px);
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "mobile/assets/shots/tablets";
    char parent[512];
    snprintf(parent, sizeof parent, "%s", out);
    for (char *q = parent + 1; *q; q++)
        if (*q == '/') { *q = 0; mkdir(parent, 0755); *q = '/'; }
    mkdir(out, 0755);
    char root[] = "/tmp/os999-tablet-shots-XXXXXX";
    if (!mkdtemp(root) || volume_seed(root) != 0) return 1;
    Session *s = session_create(root);
    if (!s) return 1;
    kbd_load_fonts("fonts/ChicagoFLF.ttf");
    mobile_pointer_load("assets/icons/cursors/arrow.curs");
    MobileShell m;
    if (mobile_shell_init(&m, s, (MobileShellHost){0}, root) != 0) return 1;
    mobile_shell_show_guide(&m, 0);
    int opened = 0;
    for (int d = 0; d < mobile_device_count(); d++) {
        const MobileDeviceProfile *dev = mobile_device_at(d);
        if (!dev->tablet) continue;
        MobileScreen sc;
        mobile_device_screen(dev, MP_AUTO, &sc);
        sc.hw_keyboard = sc.pointer = 1;
        mobile_shell_set_screen(&m, &sc, now);
        m.toast.active = 0;
        kbd_hw_key(m.kbd, 4, 1, now);   /* typing on the hardware keyboard wakes its strip */
        kbd_hw_key(m.kbd, 4, 0, now + 40);
        frames(&m, 40);
        if (!opened) {   /* double-click the startup disk with the pointer */
            Rect icon, hit;
            desktop_icon_rect(s->sw, s->sh, 0, &icon, &hit);
            click_desk(&m, hit.x + hit.w / 2, hit.y + hit.h / 2, 1);
            click_desk(&m, hit.x + hit.w / 2, hit.y + hit.h / 2, 2);
            opened = 1;
        }
        hover_desk(&m, s->sw * 3 / 5, s->sh * 2 / 5);
        frames(&m, 2);
        int k = m.canvas.w > 2400 ? 2 : 1;
        char name[96];
        snprintf(name, sizeof name, "%s-keyboard.png", dev->id);
        Img shot = copy_canvas(&m, k);
        save(out, name, shot, 1);
        printf("%-22s screen %4dx%-4d desktop %4dx%-4d at %dx\n", dev->id, sc.w, sc.h,
               m.layout.desk_w, m.layout.desk_h, m.layout.scale);
        free(shot.px);
        if (strcmp(dev->id, "ipad-pro-13") != 0) continue;
        Img mocks[3];
        /* 1: keyboard case with trackpad, Control Strip open */
        m.strip.expanded = 1;
        hover_desk(&m, s->sw / 2, s->sh / 2);
        frames(&m, 2);
        shot = copy_canvas(&m, 2);
        mocks[0] = mockup(&shot, STYLE_CASE);
        free(shot.px);
        /* 2: folio, the File menu pulled down with the pointer */
        m.strip.expanded = 0;
        float sx, sy;
        mobile_view_to_screen(&m.view, &m.layout, 52, 10, &sx, &sy);
        mobile_shell_mouse(&m, MSH_MOVE, sx, sy, 0, 0, now);
        mobile_shell_mouse(&m, MSH_DOWN, sx, sy, MSH_BUTTON_PRIMARY, 1, now += 20);
        mobile_view_to_screen(&m.view, &m.layout, 70, 64, &sx, &sy);
        mobile_shell_mouse(&m, MSH_MOVE, sx, sy, 0, 0, now += 120);
        frames(&m, 2);
        shot = copy_canvas(&m, 2);
        mocks[1] = mockup(&shot, STYLE_FOLIO);
        free(shot.px);
        mobile_shell_mouse(&m, MSH_UP, -50, -50, MSH_BUTTON_PRIMARY, 1, now += 20);
        mobile_shell_key(&m, 27, 0);
        frames(&m, 2);
        /* 3: Bluetooth keyboard, holding Command shows the shortcut sheet */
        kbd_hw_key(m.kbd, 227, 1, now);
        frames(&m, 60);
        shot = copy_canvas(&m, 2);
        mocks[2] = mockup(&shot, STYLE_BLUETOOTH);
        free(shot.px);
        kbd_hw_key(m.kbd, 227, 0, now);
        save(out, "case-trackpad.png", mocks[0], 2);
        save(out, "folio.png", mocks[1], 2);
        save(out, "bluetooth.png", mocks[2], 2);
        sheet(out, mocks, 3);
        for (int i = 0; i < 3; i++) free(mocks[i].px);
    }
    mobile_shell_free(&m);
    session_destroy(s);
    return 0;
}
