/* The Memory mode choice of an application's Get Info, driven through the real dialog (clicks and keys), plus the
 * built-in compatibility list and the launch order user override > known list > classifier. Headless; the application
 * is a synthetic 68K program authored here. */
#include "app_mode.h"
#include "../src/session_int.h"
#include "../src/native_policy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); fails++; } } while (0)
static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void p32(uint8_t *p, uint32_t v) { p16(p, v >> 16); p16(p + 2, v & 0xffff); }
#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

/* CODE 0 (jump table header), CODE 1 (tag distinguishes executables) and a SIZE resource with the 32-bit flag set. */
static size_t build_app_fork(uint8_t *out, uint8_t tag) {
    static const uint8_t code0[32] = {0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0x10};
    uint8_t body[4] = {0x4e, 0x75, 0x4e, tag}, size[10] = {0x58, 0xe0, 0, 0x20, 0, 0, 0, 0x10, 0, 0};
    const uint8_t *data[3] = {code0, body, size};
    size_t len[3] = {sizeof code0, 4, 10};
    uint32_t type[3] = {FOURCC('C', 'O', 'D', 'E'), FOURCC('C', 'O', 'D', 'E'), FOURCC('S', 'I', 'Z', 'E')};
    int id[3] = {0, 1, -1};
    size_t datalen = 0;
    for (int i = 0; i < 3; i++) datalen += 4 + ((len[i] + 1) & ~(size_t)1);
    size_t map = 256 + datalen, tl = 28, maplen = tl + 2 + 8 * 3 + 12 * 3;
    memset(out, 0, map + maplen);
    p32(out, 256); p32(out + 4, (uint32_t)map); p32(out + 8, (uint32_t)datalen); p32(out + 12, (uint32_t)maplen);
    size_t at = 256;
    for (int i = 0; i < 3; i++) {
        p32(out + at, (uint32_t)len[i]); memcpy(out + at + 4, data[i], len[i]);
        p32(out + map + tl + 2 + 24 + 12 * i + 4, (uint32_t)(at - 256) & 0xffffff);
        at += 4 + ((len[i] + 1) & ~(size_t)1);
    }
    p16(out + map + 24, (unsigned)tl); p16(out + map + 26, (unsigned)maplen); p16(out + map + tl, 2);
    for (int i = 0; i < 3; i++) {
        uint8_t *t = out + map + tl + 2 + 8 * i;
        p32(t, type[i]); p16(t + 4, 0); p16(t + 6, (unsigned)(2 + 24 + 12 * i));
        p16(out + map + tl + 2 + 24 + 12 * i, (unsigned)id[i]); p16(out + map + tl + 2 + 24 + 12 * i + 2, 0xffff);
    }
    return map + maplen;
}

static char root[256];
static void write_app(uint8_t tag) {
    uint8_t f[1024];
    size_t n = build_app_fork(f, tag);
    char path[512];
    snprintf(path, sizeof path, "%s/Applications/Game", root);
    FILE *d = fopen(path, "wb"); if (d) { fputc(0, d); fclose(d); }
    snprintf(path, sizeof path, "%s/Applications/Game.rsrc", root);
    FILE *r = fopen(path, "wb"); if (r) { fwrite(f, 1, n, r); fclose(r); }
}

static void select_game(Session *s) {
    Window *w = &s->win[0];
    snprintf(w->rel, sizeof w->rel, "Applications");
    reload_folders(s);
    for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = !strcmp(window_items(w)[i].name, "Game");
}

static void click_row(Session *s, int row) {
    Rect box, cancel, ok;
    dialog_layout(s, &box, &cancel, &ok);
    session_down(s, box.x + 40, box.y + APP_MODE_ROWS_TOP + row * 26 + 8, 1);
    session_up(s, box.x + 40, box.y + APP_MODE_ROWS_TOP + row * 26 + 8);
}

static int saved(uint64_t id, LaunchOverride *o) {
    uint64_t stale;
    return launch_override_find(root, id, 0, o, &stale);
}

int main(void) {
    snprintf(root, sizeof root, "/tmp/app-mode-XXXXXX");
    if (!mkdtemp(root)) return 2;
    CHECK(volume_seed(root) == 0);
    write_app(0x75);
    Session *s = session_create(root);
    CHECK(s != NULL);
    if (!s) return 1;
    select_game(s);
    const Item *game = NULL;
    for (int i = 0; i < s->win[0].nitems; i++) if (!strcmp(window_items(&s->win[0])[i].name, "Game")) game = &window_items(&s->win[0])[i];
    CHECK(game && game->kind == ITEM_APPLICATION);

    char full[512];
    snprintf(full, sizeof full, "%s/Applications/Game", root);
    AppMode m;
    CHECK(app_mode_load(&m, root, full) == 0 && m.c.profile == APP_PROFILE_B_CLEAN_68K && m.mode == APP_MODE_AUTOMATIC && !m.has_override);
    uint64_t id = m.c.identity;
    CHECK(id != 0 && strstr(m.summary, "classifier"));

    /* Get Info on an application opens the Memory mode rows; Automatic is selected when nothing is saved. */
    session_command(s, CMD_GET_INFO);
    CHECK(s->dialog == 4 && s->settings_kind == CMD_APP_MODE && s->settings_flags == APP_MODE_AUTOMATIC);
    CHECK(strstr(s->dialog_body, "Name: Game") && strstr(s->dialog_body, "Classic application"));

    /* Click 32-bit, OK: the override is saved. */
    click_row(s, APP_MODE_32BIT);
    CHECK(s->settings_flags == APP_MODE_32BIT);
    session_key(s, '\r', 0);
    LaunchOverride o;
    CHECK(s->dialog == 0 && saved(id, &o) == 1 && o.addr == 32 && o.profile == APP_PROFILE_B_CLEAN_68K);

    /* It persists: a fresh dialog (and a fresh model) show it. */
    session_command(s, CMD_GET_INFO);
    CHECK(s->dialog == 4 && s->settings_flags == APP_MODE_32BIT);
    CHECK(app_mode_load(&m, root, full) == 0 && m.has_override && m.mode == APP_MODE_32BIT && strstr(m.summary, "your saved choice"));

    /* Choose 24-bit with the keyboard and apply: profile A, 24-bit. Cancel leaves the saved choice alone. */
    click_row(s, APP_MODE_24BIT);
    session_key(s, 27, 0);
    CHECK(saved(id, &o) == 1 && o.addr == 32);
    session_command(s, CMD_GET_INFO);
    click_row(s, APP_MODE_24BIT);
    session_key(s, '\r', 0);
    CHECK(saved(id, &o) == 1 && o.addr == 24 && o.profile == APP_PROFILE_A_LEGACY_68K);

    /* The launch policy uses it: A on the 32-bit default asks for 24-bit, and says "override". */
    NativeSettings st = {"8", 1, 1, "", 0};
    LaunchPolicy p; char line[1024], err[256];
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(strstr(line, "source=override") && p.nargs == 2 && !strcmp(p.args[0], "--addressing") && !strcmp(p.args[1], "24"));

    /* Automatic clears it. */
    session_command(s, CMD_GET_INFO);
    click_row(s, APP_MODE_AUTOMATIC);
    session_key(s, '\r', 0);
    CHECK(saved(id, &o) == 0);

    /* The built-in list: user override > known list > classifier. A synthetic identity, no real program involved. */
    CompatKnown table[1] = {{id, APP_PROFILE_A_LEGACY_68K, "synthetic entry"}};
    compat_known_set_table(table, 1);
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(strstr(line, "source=known-list") && strstr(line, "profile=A-legacy-68k") && strstr(line, "synthetic entry") && p.nargs == 2 && !strcmp(p.args[1], "24"));
    CHECK(app_mode_load(&m, root, full) == 0 && !m.has_override && m.known && strstr(m.summary, "built-in list") && m.mode == APP_MODE_AUTOMATIC);
    session_command(s, CMD_GET_INFO);
    click_row(s, APP_MODE_32BIT);
    session_key(s, '\r', 0);   /* the user's choice beats the list */
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0);
    CHECK(strstr(line, "source=override") && p.nargs == 4 && !strcmp(p.args[1], "32"));
    CHECK(saved(id, &o) == 1);
    launch_override_clear(root, id);
    compat_known_set_table(NULL, 0);   /* back to the built-in table: this synthetic program is not in it */
    CHECK(native_policy_prepare(root, full, &st, &p, line, sizeof line, err, sizeof err) == 0 && strstr(line, "source=classifier") && p.nargs == 0);
    CHECK(compat_known_lookup(0) == NULL && compat_known_lookup(0x6f7c7bb26ad8adc6ull) != NULL && compat_known_lookup(0x6f7c7bb26ad8adc7ull) == NULL);

    /* Changing the executable invalidates the saved choice, and opening Get Info shows Automatic again. */
    session_command(s, CMD_GET_INFO);
    click_row(s, APP_MODE_32BIT);
    session_key(s, '\r', 0);
    CHECK(saved(id, &o) == 1);
    write_app(0x71);
    CHECK(app_mode_load(&m, root, full) == 0 && m.c.identity != id && !m.has_override && m.mode == APP_MODE_AUTOMATIC);
    CHECK(saved(id, &o) == 0);   /* the stale entry was dropped when the new executable was looked at */
    session_command(s, CMD_GET_INFO);
    CHECK(s->dialog == 4 && s->settings_flags == APP_MODE_AUTOMATIC);
    session_key(s, 27, 0);

    /* A plain file still gets the ordinary Get Info. */
    snprintf(full, sizeof full, "%s/Read Me", root);
    CHECK(app_mode_load(&m, root, full) == -1);

    session_destroy(s);
    char cmd[300];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd)) fails++;
    if (fails) { fprintf(stderr, "%d failure(s)\n", fails); return 1; }
    puts("app mode: all checks passed");
    return 0;
}
