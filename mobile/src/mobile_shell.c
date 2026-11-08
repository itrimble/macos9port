/* Phone shell: configuration and per-frame composition. Input lives in
 * mobile_shell_input.c. */
#include "mobile_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *const shortcuts[][2] = {
    {"N", "New Folder"}, {"O", "Open"}, {"W", "Close"}, {"T", "Next Theme"}, {"Q", "Quit"}};

/* Build (or rebuild at a new density) the keyboard, carrying its state over. */
static int ensure_kbd(MobileShell *m, float dp) {
    if (m->kbd && m->kbd_dp > dp - 0.01f && m->kbd_dp < dp + 0.01f) return 0;
    Kbd *k = kbd_create(dp);
    if (!k) return -1;
    if (m->kbd) {
        *kbd_settings(k) = *kbd_settings(m->kbd);
        kbd_set_skin(k, kbd_skin(m->kbd));
        kbd_show(k, kbd_visible(m->kbd));
        kbd_destroy(m->kbd);
    }
    kbd_set_shortcuts(k, shortcuts, (int)(sizeof shortcuts / sizeof *shortcuts));
    m->kbd = k;
    m->kbd_dp = dp;
    return 0;
}

int mobile_shell_init(MobileShell *m, Session *s, MobileShellHost host, const char *volume) {
    memset(m, 0, sizeof *m);
    m->s = s;
    m->host = host;
    m->locked = MP_AUTO;
    m->touch.mode = MT_DIRECT;
    mobile_view_reset(&m->view);
    snprintf(m->guide_path, sizeof m->guide_path, "%s/.os999-mobile-guide", volume ? volume : ".");
    struct stat st;
    m->coach.visible = stat(m->guide_path, &st) != 0;
    return s && ensure_kbd(m, 2.0f) == 0 ? 0 : -1;
}

void mobile_shell_free(MobileShell *m) {
    if (m->kbd) kbd_destroy(m->kbd);
    m->kbd = NULL;
    free(m->desk);
    free(m->prev_desk);
    mobile_canvas_free(&m->canvas);
    m->desk = m->prev_desk = NULL;
}

void mobile_shell_show_guide(MobileShell *m, int show) {
    m->coach.visible = show;
    if (!show) {
        FILE *f = fopen(m->guide_path, "w");
        if (f) { fputs("seen\n", f); fclose(f); }
    }
    m->dirty = 1;
}

static int ensure_desk(MobileShell *m, int w, int h) {
    size_t need = (size_t)w * h;
    if (need <= m->desk_cap) return 0;
    uint32_t *a = realloc(m->desk, need * sizeof *a), *b = NULL;
    if (a) m->desk = a;
    if (a) b = realloc(m->prev_desk, need * sizeof *b);
    if (!a || !b) return -1;
    m->prev_desk = b;
    memset(m->prev_desk, 0, need * sizeof *b);
    m->desk_cap = need;
    return 0;
}

static void configure(MobileShell *m, uint32_t ms, int announce) {
    MobilePosture p = m->locked != MP_AUTO ? m->locked : mobile_posture_detect(&m->screen);
    MobilePosture old_p = m->layout.posture;
    int old_w = m->layout.desk_w, old_h = m->layout.desk_h, had = m->configured;
    /* The slim hardware-keyboard strip keeps its own band below the desktop,
     * so the Trash and the bottom of windows stay in view (the full keys still
     * slide over the desktop and lift it, as on a phone). */
    MobileScreen eff = m->screen;
    if (m->hw_reserve > eff.inset_b) eff.inset_b = m->hw_reserve;
    mobile_layout(&eff, p, m->scale_pref, &m->layout);
    MobileLayout *l = &m->layout;
    if (ensure_desk(m, l->desk_w, l->desk_h) != 0) return;
    if (l->desk_w != old_w || l->desk_h != old_h)
        mobile_continuity_resize(&m->cont, m->s, l->desk_w, l->desk_h);
    mobile_view_reset(&m->view);
    MobileTouchMode mode = m->touch.mode;
    m->touch.mode = mode;
    mobile_touch_init(&m->touch, l, &m->view);
    int old_sw = m->canvas.w, old_sh = m->canvas.h;
    mobile_canvas_resize(&m->canvas, m->screen.w, m->screen.h);
    Kbd *old_kbd = m->kbd;
    if (ensure_kbd(m, l->dp) != 0) return;
    /* Half-folding into Laptop turns the glass a quarter: the keys on screen
     * turn with it, then flow into the deck instead of vanishing and sliding
     * back in. (Leaving Laptop puts the deck away, so nothing to carry.) */
    int turned = had && p == MP_LAPTOP && old_p != MP_LAPTOP && m->kbd == old_kbd &&
                 kbd_visible(m->kbd) && old_sw > 0 && (old_sw > old_sh) != (m->screen.w > m->screen.h);
    if (turned) kbd_rotate(m->kbd, m->screen.w, m->screen.h, 1, (KbdPosture)l->kbd_posture);
    else {
        kbd_set_surface(m->kbd, m->screen.w, m->screen.h);
        kbd_set_posture(m->kbd, (KbdPosture)l->kbd_posture);
    }
    kbd_set_split(m->kbd, l->kbd_split);
    /* Pass the platform's keyboard report on only when it changes (or the
     * keyboard is new): "not attached" also forgets that hardware keys are in
     * use, which would bring the full keys back on every rotation and fold
     * when the platform never reported the keyboard in the first place. */
    if (m->kbd != old_kbd || m->screen.hw_keyboard != m->hw_reported)
        kbd_hw_attached(m->kbd, m->screen.hw_keyboard);
    m->hw_reported = m->screen.hw_keyboard;
    if (l->kbd_always) kbd_show(m->kbd, 1);
    else if (m->screen.hw_keyboard) kbd_show(m->kbd, 1);   /* the slim hardware-keyboard strip */
    if (m->screen.pointer) m->pointer_visible = 1;
    else if (had && old_p == MP_LAPTOP) kbd_show(m->kbd, 0);
    mobile_coach_layout(&m->coach, l->region, l->dp);
    m->coach.posture = l->posture;
    m->coach.tablet = l->tablet;
    m->focus_y = l->desk_h / 2;
    m->configured = 1;
    m->dirty = 1;
    if (announce && had && p != old_p) {
        char title[48];
        snprintf(title, sizeof title, "%s", mobile_posture_name(p));
        mobile_toast_show(&m->toast, title, mobile_posture_hint(p), ms);
        mobile_shell_haptic(m, KBD_HAPTIC_MODE);
    }
}

void mobile_shell_set_screen(MobileShell *m, const MobileScreen *screen, uint32_t ms) {
    m->screen = *screen;
    mobile_touch_cancel(&m->touch);
    kbd_touch_cancel_all(m->kbd);
    for (int i = 0; i < MSH_FINGERS; i++) m->owner[i] = OWN_NONE;
    configure(m, ms, 1);
}

void mobile_shell_lock_posture(MobileShell *m, MobilePosture p, uint32_t ms) {
    m->locked = p;
    configure(m, ms, 1);
}

void mobile_shell_rescale(MobileShell *m, int scale, uint32_t ms) {
    m->scale_pref = scale;
    configure(m, ms, 0);
    char body[64];
    snprintf(body, sizeof body, "%d x %d, each pixel %d x %d on glass.",
             m->layout.desk_w, m->layout.desk_h, m->layout.scale, m->layout.scale);
    mobile_toast_show(&m->toast, "Display", body, ms);
}

static void strip_state(const MobileShell *m, MobileStripState *st) {
    st->keyboard = kbd_visible(m->kbd) && !m->layout.kbd_always;
    st->trackpad = m->touch.mode == MT_TRACKPAD;
    st->zoom = m->view.zoom;
    st->desk_w = m->layout.desk_w;
    st->desk_h = m->layout.desk_h;
    st->posture = m->layout.posture;
    st->posture_locked = m->locked != MP_AUTO;
    st->skin = kbd_skin_name(kbd_skin(m->kbd));
    st->sound = m->s->host_sound_enabled;
    st->tablet = m->layout.tablet;
    st->hw_keyboard = m->screen.hw_keyboard || kbd_hw_active(m->kbd);
}

Rect mobile_shell_strip_area(const MobileShell *m) {
    if (m->layout.kbd_always) return m->layout.region;
    int occ = kbd_occluded_height(m->kbd);
    int bottom = m->screen.h - m->screen.inset_b;
    if (occ > m->screen.inset_b) bottom = m->screen.h - occ;
    return (Rect){m->screen.inset_l, m->screen.inset_t,
                  m->screen.w - m->screen.inset_l - m->screen.inset_r, bottom - m->screen.inset_t};
}

void mobile_shell_drain(MobileShell *m);   /* mobile_shell_input.c */

/* Height of the keyboard's hardware strip (kbd_core.c: 36 dp). */
#define KBD_HW_STRIP_DP 36.0f

int mobile_shell_frame(MobileShell *m, uint32_t ms) {
    if (!m->configured) return 0;
    m->now = ms;
    if (m->layout.kbd_always && !kbd_visible(m->kbd)) kbd_show(m->kbd, 1);   /* the deck can't hide */
    /* The first hardware key press reveals the keyboard's slim strip, even if
     * the platform never reported a keyboard (some don't until it's used). */
    if (kbd_hw_active(m->kbd) && !m->hw_seen && !m->layout.kbd_always) {
        m->hw_seen = 1;
        kbd_show(m->kbd, 1);
    }
    int reserve = !m->layout.kbd_always && kbd_hw_active(m->kbd) &&
                          (kbd_occluded_height(m->kbd) > 0 || kbd_animating(m->kbd))
                      ? (int)(KBD_HW_STRIP_DP * m->kbd_dp + 0.5f) : 0;
    if (reserve != m->hw_reserve) {
        m->hw_reserve = reserve;
        configure(m, ms, 0);
    }
    kbd_tick(m->kbd, ms);
    mobile_touch_tick(&m->touch, ms);
    mobile_shell_drain(m);
    session_tick(m->s, ms);
    mobile_shell_follow_edit(m);
    MobileLayout *l = &m->layout;
    int occ = l->kbd_always ? 0 : kbd_occluded_height(m->kbd);
    mobile_view_lift(&m->view, l, m->focus_y, occ, m->screen.h - m->screen.inset_b);
    session_paint(m->s, m->desk, l->desk_w, l->desk_h);
    size_t bytes = (size_t)l->desk_w * l->desk_h * sizeof *m->desk;
    int changed = memcmp(m->desk, m->prev_desk, bytes) != 0;
    if (changed) memcpy(m->prev_desk, m->desk, bytes);
    int busy = kbd_animating(m->kbd) || m->toast.active || m->touch.loupe ||
               m->view.lift != m->last_lift;
    if (!changed && !m->dirty && !busy) return 0;
    m->last_lift = m->view.lift;
    m->dirty = 0;

    mobile_compose_desk(&m->canvas, l, &m->view, m->desk, 0xFF1A1A1Au);
    Fb fb;
    fb_init(&fb, m->canvas.px, m->canvas.w, m->canvas.h);
    if (l->kbd_always) mobile_compose_deck(&m->canvas, l->deck, 0xFF4A4A4Au, 0xFF2C2C2Cu);
    kbd_draw(m->kbd, &fb);   /* keys, the hardware strip, or the shortcut sheet */
    if (m->touch.mode == MT_TRACKPAD || m->pointer_visible)
        mobile_pointer_draw(&fb, l, &m->view, m->touch.px, m->touch.py);
    MobileStripState st;
    strip_state(m, &st);
    mobile_strip_place(&m->strip, mobile_shell_strip_area(m), l->dp);
    mobile_strip_draw(&m->strip, &fb, &st);
    mobile_loupe_draw(&fb, l, &m->view, &m->touch, m->desk);
    mobile_toast_draw(&m->toast, &fb, l->region, l->dp, ms);
    mobile_coach_draw(&m->coach, &fb, l->dp);
    return 1;
}
