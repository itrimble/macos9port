#include "app_mode.h"
#include "desktop_theme.h"
#include "native_policy.h"
#include "session_int.h"

#include <stdio.h>
#include <string.h>

static AppMode g_mode;
static int g_open;

const char *app_mode_label(int mode) {
    return mode == APP_MODE_24BIT ? "24-bit (16 MB)" : mode == APP_MODE_32BIT ? "32-bit" : "Automatic";
}

static const char *source_text(const AppMode *m) {
    return m->has_override ? "your saved choice" : m->known ? "the built-in list" : "the classifier";
}

int app_mode_load(AppMode *m, const char *root, const char *full) {
    memset(m, 0, sizeof *m);
    int kind = native_policy_classify(root, full, &m->c, &m->slot, m->root, sizeof m->root);
    if (kind == 1) return -1;
    if (m->c.identity) {
        uint64_t stale = 0;
        m->has_override = launch_override_find(m->root, m->c.identity, m->slot, &m->ov, &stale) == 1;
    }
    m->known = m->has_override ? NULL : compat_known_lookup(m->c.identity);
    if (m->has_override) m->mode = m->ov.addr == 24 ? APP_MODE_24BIT : m->ov.addr == 32 ? APP_MODE_32BIT : APP_MODE_AUTOMATIC;
    AppProfile shown = m->c.profile;
    if (m->has_override) shown = m->ov.profile;
    else if (m->known) shown = m->known->profile;
    snprintf(m->summary, sizeof m->summary, "Profile %s, chosen by %s.\n%s%s%s", app_profile_name(shown), source_text(m),
             m->known && !m->has_override ? m->known->reason : m->c.reason, m->c.missing[0] ? "\nMissing: " : "", m->c.missing);
    return 0;
}

int app_mode_save(const AppMode *m, int mode) {
    if (!m->c.identity || mode < 0 || mode >= APP_MODE_COUNT) return -1;
    if (mode == APP_MODE_AUTOMATIC) return launch_override_clear(m->root, m->c.identity);
    LaunchOverride o = {m->c.identity, m->slot, APP_PROFILE_B_CLEAN_68K, mode == APP_MODE_24BIT ? 24 : 32, 0};
    if (mode == APP_MODE_24BIT) o.profile = APP_PROFILE_A_LEGACY_68K;
    else if (m->c.profile == APP_PROFILE_C_CLASSIC_PPC) o.profile = APP_PROFILE_C_CLASSIC_PPC;
    return launch_override_set(m->root, &o);
}

int app_mode_open(Session *s, const char *rel, const char *info) {
    char full[OSPI_PATH];
    if (!s || !rel || volume_join(full, sizeof full, s->root, rel) != 0 || app_mode_load(&g_mode, s->root, full)) return -1;
    if (g_mode.c.profile == APP_PROFILE_D_UNSUPPORTED && !g_mode.has_override) return -1;   /* nothing to choose: the plain panel explains */
    dialog_info(s, "Get Info", info);
    s->settings_kind = CMD_APP_MODE;
    s->settings_row = g_mode.mode;
    s->settings_flags = g_mode.mode;
    s->dialog = 4;
    s->dialog_cmd = CMD_SETTINGS_APPLY;
    g_open = 1;
    return 0;
}

int app_mode_active(const Session *s) { return g_open && s->dialog == 4 && s->settings_kind == CMD_APP_MODE; }

void app_mode_choose(Session *s, int row) {
    if (row >= 0 && row < APP_MODE_COUNT) s->settings_flags = row;
}

int app_mode_apply(Session *s) {
    int ok = g_open && s->settings_flags != g_mode.mode ? app_mode_save(&g_mode, s->settings_flags) == 0 : 1;
    g_open = 0;
    if (!ok) dialog_info(s, "Memory mode", "The choice could not be saved on this volume.");
    return ok ? 0 : -1;
}

void app_mode_paint(Fb *fb, const Session *s, Rect box) {
    if (!app_mode_active(s)) return;
    int x = box.x + 20, y = box.y + APP_MODE_ROWS_TOP - 20;
    uint32_t ink = desktop_theme_color(THEME_FOREGROUND);
    fb_text(fb, x, y, ink, "Memory mode");
    int top = box.y + APP_MODE_ROWS_TOP + APP_MODE_COUNT * 26 + 2;
    fb_clip(fb, box.x + 12, top, box.w - 24, box.y + box.h - 44 - top);
    text_draw(fb, x, top, box.w - 40, ink, g_mode.summary);
    fb_unclip(fb);
}
