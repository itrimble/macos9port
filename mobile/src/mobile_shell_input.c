/* Phone shell input: who owns each finger, strip actions, and turning
 * gesture and keyboard events into desktop calls. */
#include "mobile_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int slot(MobileShell *m, int64_t id, int make) {
    for (int i = 0; i < MSH_FINGERS; i++)
        if (m->owner[i] != OWN_NONE && m->finger_id[i] == id) return i;
    if (!make) return -1;
    for (int i = 0; i < MSH_FINGERS; i++)
        if (m->owner[i] == OWN_NONE) { m->finger_id[i] = id; return i; }
    return -1;
}

static void take_clip(MobileShell *m) {
    if (!m->host.clipboard) return;
    char *text = m->host.clipboard(m->host.ctx);
    session_set_clip(m->s, text ? text : "");
    free(text);
}

void mobile_shell_haptic(MobileShell *m, int type) {
    if (!m->host.haptic || !kbd_settings(m->kbd)->haptics) return;
    KbdHapticSpec spec;
    kbd_haptic_spec(type, &spec);
    float k = kbd_settings(m->kbd)->haptic_strength;
    m->host.haptic(m->host.ctx, type, spec.intensity * k, spec.sharpness, spec.duration_ms);
}

static void pointer_to(MobileShell *m, float x, float y) {
    MobileLayout *l = &m->layout;
    m->touch.px = x < 0 ? 0 : (x >= l->desk_w ? l->desk_w - 1 : x);
    m->touch.py = y < 0 ? 0 : (y >= l->desk_h ? l->desk_h - 1 : y);
    session_move(m->s, (int)m->touch.px, (int)m->touch.py);
    m->focus_y = (int)m->touch.py;
}

static void apply_action(MobileShell *m, const MobileAction *a) {
    Session *s = m->s;
    switch (a->kind) {
    case MA_MOVE:
        session_move(s, a->x, a->y);
        m->focus_y = a->y;
        break;
    case MA_DOWN:
        take_clip(m);
        s->input_now_ms = m->now;
        session_down(s, a->x, a->y, a->clicks);
        break;
    case MA_UP: session_up(s, a->x, a->y); break;
    case MA_CONTEXT: session_context(s, a->x, a->y); break;
    case MA_WHEEL: session_wheel(s, a->lines); break;
    case MA_HAPTIC: mobile_shell_haptic(m, a->haptic); break;
    default: break;
    }
    m->dirty = 1;
}

static void apply_kbd(MobileShell *m, const KbdEvent *e) {
    float k = (float)m->layout.scale * m->view.zoom;
    switch (e->kind) {
    case KBD_EV_KEY:
        if (!e->down) break;
        if ((e->mods & KBD_MOD_CMD) && e->key == 'v') take_clip(m);   /* paste the phone's clipboard */
        session_key(m->s, e->key, (e->mods & KBD_MOD_CMD) ? MOD_CMD : 0);
        break;
    case KBD_EV_TEXT:
        session_text(m->s, e->text);   /* keeps capitals, symbols and Option letters */
        break;
    case KBD_EV_FORCE: {               /* as in the Finder: Force Click a name to rename it */
        int x = (int)m->touch.px, y = (int)m->touch.py;
        if (m->s->down) session_up(m->s, x, y);
        session_rename_at(m->s, x, y);
        break;
    }
    case KBD_EV_POINTER:
        m->pointer_visible = 1;
        pointer_to(m, m->touch.px + e->dx / k, m->touch.py + e->dy / k);
        break;
    case KBD_EV_BUTTON:
        if (e->down && (e->mods & KBD_MOD_CTRL)) {
            session_context(m->s, (int)m->touch.px, (int)m->touch.py);
        } else if (e->down) {
            take_clip(m);
            m->s->input_now_ms = m->now;
            session_down(m->s, (int)m->touch.px, (int)m->touch.py, e->clicks > 0 ? e->clicks : 1);
        } else {
            session_up(m->s, (int)m->touch.px, (int)m->touch.py);
        }
        break;
    case KBD_EV_SCROLL: {
        m->wheel_acc += e->dy;
        int lines = (int)m->wheel_acc;
        if (lines) { m->wheel_acc -= lines; session_wheel(m->s, lines); }
        break;
    }
    case KBD_EV_HAPTIC:
        if (m->host.haptic) m->host.haptic(m->host.ctx, e->haptic, e->strength, e->sharpness, e->duration_ms);
        break;
    default: break;
    }
    if (m->host.kbd_event) m->host.kbd_event(m->host.ctx, e);
    m->dirty = 1;
}

void mobile_shell_drain(MobileShell *m) {
    KbdEvent ke;
    while (kbd_poll_event(m->kbd, &ke)) apply_kbd(m, &ke);
    MobileAction a;
    while (mobile_touch_poll(&m->touch, &a)) apply_action(m, &a);
}

enum { MOUSE_NONE, MOUSE_DESK, MOUSE_STRIP, MOUSE_KBD, MOUSE_COACH };
#define MOUSE_FINGER ((int64_t)0x4D4F555345)   /* the mouse as a keyboard "finger" */

void mobile_shell_mouse(MobileShell *m, int phase, float x, float y, int button, int clicks,
                        uint32_t ms) {
    if (!m->configured) return;
    m->now = ms;
    m->pointer_visible = 1;
    m->dirty = 1;
    int dx, dy;
    mobile_view_to_desk(&m->view, &m->layout, x, y, &dx, &dy);
    if (phase == MSH_DOWN) {
        int hit;
        if (m->coach.visible) { m->mouse_owner = MOUSE_COACH; return; }
        if ((hit = mobile_strip_hit(&m->strip, (int)x, (int)y)) != MS_NONE) {
            m->mouse_owner = MOUSE_STRIP;
            m->strip.pressed = hit;
            return;
        }
        if (kbd_touch_down(m->kbd, MOUSE_FINGER, x, y, ms)) { m->mouse_owner = MOUSE_KBD; return; }
        if (m->strip.expanded) m->strip.expanded = 0;
        m->mouse_owner = MOUSE_DESK;
        pointer_to(m, (float)dx, (float)dy);
        if (button == MSH_BUTTON_SECONDARY) {
            session_context(m->s, dx, dy);
            m->mouse_down = 0;
            return;
        }
        take_clip(m);
        m->s->input_now_ms = ms;
        session_down(m->s, dx, dy, clicks > 0 ? clicks : 1);
        m->mouse_down = 1;
        return;
    }
    if (phase == MSH_MOVE) {
        if (m->mouse_owner == MOUSE_KBD) kbd_touch_move(m->kbd, MOUSE_FINGER, x, y, ms);
        else if (m->mouse_owner != MOUSE_STRIP && m->mouse_owner != MOUSE_COACH)
            pointer_to(m, (float)dx, (float)dy);
        return;
    }
    switch (m->mouse_owner) {   /* MSH_UP */
    case MOUSE_STRIP: {
        int hit = mobile_strip_hit(&m->strip, (int)x, (int)y), pressed = m->strip.pressed;
        m->strip.pressed = MS_NONE;
        if (hit == pressed) mobile_shell_strip_action(m, hit, ms);
        break;
    }
    case MOUSE_KBD: kbd_touch_up(m->kbd, MOUSE_FINGER, x, y, ms); break;
    case MOUSE_COACH:
        if (mobile_coach_hit_button(&m->coach, (int)x, (int)y)) mobile_shell_show_guide(m, 0);
        break;
    case MOUSE_DESK:
        pointer_to(m, (float)dx, (float)dy);
        if (m->mouse_down) session_up(m->s, dx, dy);
        break;
    default: break;
    }
    m->mouse_owner = MOUSE_NONE;
    m->mouse_down = 0;
}

void mobile_shell_magnify(MobileShell *m, float factor) {
    if (!m->configured || !(factor > 0)) return;
    const MobileLayout *l = &m->layout;
    float sx = l->desk.x + l->desk.w / 2.0f, sy = l->desk.y + l->desk.h / 2.0f;
    if (m->pointer_visible || m->touch.mode == MT_TRACKPAD)
        mobile_view_to_screen(&m->view, l, m->touch.px, m->touch.py, &sx, &sy);
    mobile_view_zoom_at(&m->view, l, m->view.zoom * factor, sx, sy);
    m->dirty = 1;
}

void mobile_shell_wheel(MobileShell *m, float lines) {
    m->wheel_acc += lines;
    int n = (int)m->wheel_acc;
    if (n) { m->wheel_acc -= n; session_wheel(m->s, n); m->dirty = 1; }
}

void mobile_shell_key(MobileShell *m, int key, int mods) {
    if ((mods & MOD_CMD) && key == 'v') take_clip(m);
    session_key(m->s, key, mods);
    m->dirty = 1;
}

void mobile_shell_text(MobileShell *m, const char *utf8) {
    session_text(m->s, utf8);
    m->dirty = 1;
}

void mobile_shell_finger_force(MobileShell *m, int64_t id, float pressure, uint32_t ms) {
    int i = slot(m, id, 0);
    if (i >= 0 && m->owner[i] == OWN_KBD) kbd_touch_force(m->kbd, id, pressure, ms);
}

void mobile_shell_follow_edit(MobileShell *m) {
    int editing = session_editing(m->s);
    if (editing != m->was_editing) {
        m->was_editing = editing;
        if (editing && !kbd_visible(m->kbd) && !m->layout.kbd_always && !kbd_hw_active(m->kbd)) {
            kbd_show(m->kbd, 1);   /* like tapping into a text field */
            m->kbd_for_edit = 1;
        } else if (!editing && m->kbd_for_edit) {
            if (!m->layout.kbd_always) kbd_show(m->kbd, 0);
            m->kbd_for_edit = 0;
        }
        m->dirty = 1;
    }
    Rect r;
    if (editing && session_edit_rect(m->s, &r)) m->focus_y = r.y + r.h + 4;   /* keep it above the keys */
}

static MobilePosture next_posture(MobilePosture p) {
    switch (p) {
    case MP_AUTO: return MP_COVER;
    case MP_COVER: return MP_FLAT;
    case MP_FLAT: return MP_LAPTOP;
    case MP_LAPTOP: return MP_BOOK;
    default: return MP_AUTO;
    }
}

void mobile_shell_strip_action(MobileShell *m, int tile, uint32_t ms) {
    MobileLayout *l = &m->layout;
    char body[96];
    switch (tile) {
    case MS_TAB: m->strip.expanded = !m->strip.expanded; break;
    case MS_KEYBOARD:
        if (l->kbd_always)
            mobile_toast_show(&m->toast, "Keyboard", "In Laptop mode the keyboard lives below the fold.", ms);
        else kbd_show(m->kbd, !kbd_visible(m->kbd));
        break;
    case MS_POINTER:
        mobile_touch_set_mode(&m->touch, m->touch.mode == MT_DIRECT ? MT_TRACKPAD : MT_DIRECT);
        m->pointer_visible = m->touch.mode == MT_TRACKPAD;
        mobile_toast_show(&m->toast, m->touch.mode == MT_TRACKPAD ? "Trackpad" : "Touch",
                          m->touch.mode == MT_TRACKPAD
                              ? "Slide to move the pointer, tap to click, two fingers to scroll."
                              : "Tap what you see. Hold to aim with the loupe.", ms);
        break;
    case MS_ZOOM:
        if (m->view.zoom > 1.01f) mobile_view_reset(&m->view);
        else mobile_view_zoom_at(&m->view, l, 2.0f, l->desk.x + l->desk.w / 2.0f,
                                 l->desk.y + l->desk.h / 2.0f);
        break;
    case MS_DISPLAY: mobile_shell_rescale(m, mobile_layout_cycle_scale(l, +1), ms); return;
    case MS_POSTURE: {
        MobilePosture p = next_posture(m->locked);
        mobile_shell_lock_posture(m, p, ms);
        if (p == MP_AUTO) mobile_toast_show(&m->toast, "Automatic", "Postures follow the hinge again.", ms);
        return;
    }
    case MS_SKIN: {
        int skin = (kbd_skin(m->kbd) + 1) % kbd_skin_count();
        kbd_set_skin(m->kbd, skin);
        if (!kbd_visible(m->kbd)) kbd_show(m->kbd, 1);
        snprintf(body, sizeof body, "Keyboard skin %d of %d.", skin + 1, kbd_skin_count());
        mobile_toast_show(&m->toast, kbd_skin_name(skin), body, ms);
        break;
    }
    case MS_SOUND:
        session_command(m->s, CMD_SOUND_TOGGLE);
        kbd_settings(m->kbd)->sounds = !kbd_settings(m->kbd)->sounds;
        break;
    case MS_HELP: mobile_shell_show_guide(m, 1); break;
    default: return;
    }
    mobile_shell_haptic(m, KBD_HAPTIC_TAP);
    m->dirty = 1;
}

void mobile_shell_finger(MobileShell *m, int phase, int64_t id, float x, float y, uint32_t ms) {
    if (!m->configured) return;
    m->now = ms;
    if (phase == MSH_CANCEL) {
        mobile_touch_cancel(&m->touch);
        kbd_touch_cancel_all(m->kbd);
        for (int i = 0; i < MSH_FINGERS; i++) m->owner[i] = OWN_NONE;
        m->strip.pressed = MS_NONE;
        m->dirty = 1;
        return;
    }
    int i = slot(m, id, phase == MSH_DOWN);
    if (i < 0) return;
    if (phase == MSH_DOWN) {
        int hit = MS_NONE;
        if (m->coach.visible) m->owner[i] = OWN_COACH;
        else if ((hit = mobile_strip_hit(&m->strip, (int)x, (int)y)) != MS_NONE) {
            m->owner[i] = OWN_STRIP;
            m->strip.pressed = hit;
        } else if ((kbd_visible(m->kbd) || m->layout.kbd_always) &&
                   kbd_touch_down(m->kbd, id, x, y, ms)) {
            m->owner[i] = OWN_KBD;
        } else {
            if (m->strip.expanded) m->strip.expanded = 0;   /* tapping away tucks it in */
            if (m->touch.mode == MT_DIRECT) m->pointer_visible = 0;
            m->owner[i] = OWN_DESK;
            mobile_touch_down(&m->touch, id, x, y, ms);
        }
        m->dirty = 1;
        return;
    }
    int own = m->owner[i];
    if (own == OWN_DESK) {
        if (phase == MSH_MOVE) mobile_touch_move(&m->touch, id, x, y, ms);
        else mobile_touch_up(&m->touch, id, x, y, ms);
    } else if (own == OWN_KBD) {
        if (phase == MSH_MOVE) kbd_touch_move(m->kbd, id, x, y, ms);
        else kbd_touch_up(m->kbd, id, x, y, ms);
    } else if (own == OWN_STRIP && phase == MSH_UP) {
        int hit = mobile_strip_hit(&m->strip, (int)x, (int)y);
        int pressed = m->strip.pressed;
        m->strip.pressed = MS_NONE;
        if (hit == pressed) mobile_shell_strip_action(m, hit, ms);
    } else if (own == OWN_COACH && phase == MSH_UP) {
        if (mobile_coach_hit_button(&m->coach, (int)x, (int)y)) mobile_shell_show_guide(m, 0);
    }
    if (phase == MSH_UP) m->owner[i] = OWN_NONE;
    m->dirty = 1;
}
