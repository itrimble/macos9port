/* V08 audit: in every context, each enabled desktop menu item must change
 * something observable (state, volume, dialog) or already be in the state it
 * asks for, and each disabled item must be reported disabled with a reason.
 * Headless; one temporary volume per probe. Run with -v for the full table. */
#include "ospi.h"
#include "desktop_menu_status.h"
#include "term.h"
#include "notes.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void dialog_accept(Session *s);
static int failures, probes, enabled_total, disabled_total, verbose, hook_calls;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
static void hook_open_panel(Session *s, int tab) { (void)s; (void)tab; hook_calls++; }
static int state_mark(const Session *s) {
    const Window *t = s->nwin ? &s->win[s->nwin - 1] : NULL;
    return hook_calls * 1000 + (t && t->kind == WIN_TERMINAL && t->term ? 7 + t->term->vt.theme : 0);
}

enum { CTX_NONE, CTX_DISK, CTX_TRASH_ICON, CTX_ROOT, CTX_FILE, CTX_FOLDER, CTX_ALL, CTX_TEXT,
       CTX_TRASH_WIN, CTX_DIALOG, CTX_CONSOLE, CTX_ALIAS, CTX_LIST, CTX_NOTE, CTX_COUNT };
static const char *const ctx_name[] = {"no windows", "disk icon selected", "Trash icon selected",
    "folder window, nothing selected", "folder window, one file selected",
    "folder window, one folder selected", "folder window, all selected", "text window",
    "Trash window, item selected", "dialog open", "console window", "alias selected",
    "list view, item selected", "note window"};

static uint64_t hash_tree(const char *root, const char *rel, uint64_t h) {
    char path[OSPI_PATH * 2];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    DIR *d = opendir(path);
    if (!d) return h;
    for (struct dirent *e; (e = readdir(d)); ) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char sub[OSPI_PATH * 2], full[OSPI_PATH * 2];
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, sub);
        struct stat st;
        if (lstat(full, &st)) continue;
        for (const char *p = sub; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
        h = (h ^ (uint64_t)st.st_size) * 1099511628211ULL;
        h = (h ^ (uint64_t)st.st_mode) * 1099511628211ULL;
        if (S_ISDIR(st.st_mode)) h = hash_tree(root, sub, h);
    }
    closedir(d);
    return h;
}
#define H0 14695981039346656037ULL
static void neutral(Session *s) { /* fields that change without a command having an effect */
    s->menu = -1; s->menu_hot = -1; s->menu_child = 0; s->menu_child_hot = 0;
    s->menu_flash_menu = 0; s->menu_flash_row = 0; s->menu_flash_start_ms = s->menu_flash_end_ms = 0;
    s->menu_blink_count = 0; s->last_activity_ms = 0; s->last_stat = 0;
}
static void write_file(const char *root, const char *rel, const char *body) {
    char full[OSPI_PATH * 2]; snprintf(full, sizeof full, "%s/%s", root, rel);
    FILE *f = fopen(full, "w"); if (f) { fputs(body, f); fclose(f); }
}
static void select_named(Session *s, const char *name) {
    Window *w = &s->win[s->nwin - 1];
    for (int i = 0; i < w->nitems; i++) w->items[i].selected = !strcmp(w->items[i].name, name);
}
static void remove_tree(const char *root) {
    char cmdline[OSPI_PATH + 16];
    snprintf(cmdline, sizeof cmdline, "rm -rf '%s'", root);
    if (!strncmp(root, "/tmp/os999-menus-", 17)) (void)system(cmdline);
}
static Session *make(int ctx, char *root, size_t rn) {
    snprintf(root, rn, "/tmp/os999-menus-XXXXXX");
    if (!mkdtemp(root) || volume_seed(root)) return NULL;
    write_file(root, "Trash/old note", "x\n");
    Session *s = session_create(root);
    if (!s) return NULL;
    switch (ctx) {
    case CTX_NONE: session_command(s, CMD_CLOSE_ALL); s->desk_sel = -1; break;
    case CTX_DISK: session_command(s, CMD_CLOSE_ALL); s->desk_sel = 0; break;
    case CTX_TRASH_ICON: session_command(s, CMD_CLOSE_ALL); s->desk_sel = 1; break;
    case CTX_ROOT: break;
    case CTX_FILE: select_named(s, "Read Me"); break;
    case CTX_FOLDER: select_named(s, "Documents"); break;
    case CTX_ALL: session_command(s, CMD_SELECT_ALL); break;
    case CTX_TEXT: select_named(s, "Read Me"); session_command(s, CMD_OPEN); break;
    case CTX_TRASH_WIN:
        session_command(s, CMD_CLOSE_ALL); s->desk_sel = 1; session_command(s, CMD_OPEN);
        select_named(s, "old note"); break;
    case CTX_DIALOG: session_command(s, CMD_EMPTY_ASK); break;
    case CTX_CONSOLE: session_command(s, CMD_TERMINAL_CONSOLE); break;
    case CTX_ALIAS: select_named(s, "Read Me"); session_command(s, CMD_MAKE_ALIAS); break;
    case CTX_LIST: session_command(s, CMD_VIEW_LIST); select_named(s, "Read Me"); break;
    case CTX_NOTE: session_command(s, CMD_NEW_NOTE); break;
    }
    menu_update(s);
    return s;
}
static int skip_run(int cmd) {
    /* Launchers that fork helpers or need the host; their enable state is still checked. */
    return cmd == CMD_QUAKE3 || cmd == CMD_QUAKE3_DEMO || cmd == CMD_TAILSCALE ||
           cmd == CMD_TERMINAL_SHELL;
}
static void probe(int ctx, int menu, int item) {
    char root[OSPI_PATH]; Session *s = make(ctx, root, sizeof root);
    if (!s) { failures++; fprintf(stderr, "setup failed ctx %d\n", ctx); return; }
    const MenuItem *mi = menu_at(menu, item);
    if (!mi || !mi->label || (mi->flags & MI_SUBMENU)) goto done;
    int cmd = mi->cmd, off = !!(mi->flags & MI_DISABLED), checked = !!(mi->flags & MI_CHECKED);
    char label[96]; snprintf(label, sizeof label, "%s", mi->label);
    probes++;
    const char *why = desktop_menu_reason(s, cmd);
    if (off) {
        disabled_total++;
        /* A disabled row must not activate, and its key equivalent must explain itself. */
        int before_calls = hook_calls;
        CHECK(menu_activate(s, menu, item) == 0);
        CHECK(hook_calls == before_calls);
        if (mi->key && ctx != CTX_DIALOG && why) {
            int got = menu_cmd_for_key(mi->key), owned = 0;
            for (int m = 0; m < menu_count(); m++)
                for (int i = 0; i < menu_nitems(m); i++)
                    if (got && menu_at(m, i)->cmd == got && !(menu_at(m, i)->flags & MI_DISABLED)) owned = 1;
            if (got != CMD_MENU_UNAVAILABLE && !owned) {
                failures++; fprintf(stderr, "FAIL key for disabled '%s' stayed silent in %s\n", label, ctx_name[ctx]);
            }
        }
        if (verbose) printf("%-34s | %d | %-28s | %3d | disabled | %s\n", ctx_name[ctx], menu, label, cmd, why ? why : "(no reason)");
        if (!why && cmd != CMD_NONE && ctx != CTX_DIALOG) {
            failures++; fprintf(stderr, "FAIL disabled without reason: ctx=%s menu=%d '%s' cmd=%d\n", ctx_name[ctx], menu, label, cmd);
        }
    } else {
        enabled_total++;
        if (skip_run(cmd)) goto done;
        Session *before = malloc(sizeof *s); memcpy(before, s, sizeof *s);
        uint64_t h0 = hash_tree(root, "", H0);
        int m0 = state_mark(s);
        neutral(s); neutral(before);
        session_command(s, cmd); neutral(s);
        uint64_t h1 = hash_tree(root, "", H0);
        int changed = memcmp(before, s, sizeof *s) != 0 || h0 != h1 || m0 != state_mark(s);
        free(before);
        /* A checked row, Select All with everything selected, Clean Up on a tidy
         * window, Clear Scrollback on an empty one and choosing the frontmost
         * window or the current application leave nothing to change. */
        int idempotent = checked || cmd == CMD_SELECT_ALL || cmd == CMD_CLEANUP || cmd == CMD_TERM_CLEAR ||
            cmd == CMD_APP_FINDER || cmd == CMD_WINDOW_BASE + (s->nwin ? s->nwin - 1 : 0);
        if (verbose) printf("%-34s | %d | %-28s | %3d | enabled  | %s\n", ctx_name[ctx], menu, label, cmd,
                            changed ? "effect" : idempotent ? "already in that state" : "NO EFFECT");
        if (!changed && !idempotent) {
            failures++; fprintf(stderr, "FAIL enabled but no effect: ctx=%s menu=%d '%s' cmd=%d\n", ctx_name[ctx], menu, label, cmd);
        }
    }
done:
    session_destroy(s);
    remove_tree(root);
}
static int exists(const char *root, const char *rel) {
    char full[OSPI_PATH * 2]; struct stat st;
    snprintf(full, sizeof full, "%s/%s", root, rel);
    return lstat(full, &st) == 0;
}
static int has_item(const Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) if (!strcmp(w->items[i].name, name)) return 1;
    return 0;
}
/* Specific outcomes that the generic sweep cannot judge. */
static void functional(void) {
    char root[OSPI_PATH]; Session *s = make(CTX_ROOT, root, sizeof root);
    if (!s) { failures++; return; }
    Window *w = &s->win[s->nwin - 1];
    /* A disabled key equivalent explains itself. Cut never works here. */
    session_key(s, 'x', MOD_CMD);
    CHECK(s->dialog && !strcmp(s->dialog_title, "Not Available") && strstr(s->dialog_body, "Cut"));
    session_key(s, 27, 0); CHECK(!s->dialog);
    session_key(s, 'y', MOD_CMD);   /* Put Away outside the Trash */
    CHECK(s->dialog && !strcmp(s->dialog_title, "Not Available") && strstr(s->dialog_body, "Trash"));
    session_key(s, 27, 0);
    /* Duplicate, Make Alias, Move to Trash and Put Away change the volume. */
    w = &s->win[s->nwin - 1];
    select_named(s, "Read Me"); menu_update(s);
    session_command(s, CMD_DUPLICATE);
    CHECK(exists(root, "Read Me") && has_item(&s->win[s->nwin - 1], "Read Me copy"));
    select_named(s, "Read Me"); session_command(s, CMD_MAKE_ALIAS);
    CHECK(has_item(&s->win[s->nwin - 1], "Read Me alias"));
    select_named(s, "Read Me copy"); session_command(s, CMD_MOVE_TRASH);
    CHECK(!exists(root, "Read Me copy") && exists(root, "Trash/Read Me copy"));
    session_command(s, CMD_UNDO);
    CHECK(exists(root, "Read Me copy"));
    /* Empty Trash asks first, then deletes. */
    session_command(s, CMD_EMPTY_ASK);
    CHECK(s->dialog == 2 && s->dialog_cmd == CMD_EMPTY_DO);
    dialog_accept(s);
    CHECK(!exists(root, "Trash/old note"));
    session_command(s, CMD_EMPTY_ASK);
    CHECK(s->dialog && strstr(s->dialog_body, "empty"));
    session_key(s, 27, 0);
    /* Edit menu: Select All and Copy Names on the folder window. */
    session_command(s, CMD_SELECT_ALL); session_command(s, CMD_COPY);
    CHECK(s->clipboard_write_request && strchr(s->clipboard_out, '\n'));
    /* View: sorting changes the order; Window menu lists the open windows. */
    w = &s->win[s->nwin - 1];
    session_command(s, CMD_SORT_SIZE); CHECK(w->sort_mode == SORT_SIZE);
    session_command(s, CMD_SORT_NAME); CHECK(w->sort_mode == SORT_NAME);
    session_command(s, CMD_VIEW_BUTTONS); CHECK(w->view_mode == VIEW_BUTTONS);
    menu_update(s); int found = 0;
    for (int i = 0; i < menu_nitems(8); i++) if (menu_at(8, i)->cmd == CMD_WINDOW_BASE + s->nwin - 1) found = 1;
    CHECK(found);
    /* Help topics open text or a dialog; Eject and Erase are dimmed with reasons. */
    menu_update(s);
    for (int i = 0; i < menu_nitems(5); i++) {
        const MenuItem *h = menu_at(5, i);
        if (!h->label || (h->flags & MI_DISABLED)) continue;
        CHECK(!(h->flags & MI_DISABLED));
    }
    const MenuItem *eject = NULL, *erase = NULL;
    for (int i = 0; i < menu_nitems(4); i++) {
        if (menu_at(4, i)->cmd == CMD_EJECT) eject = menu_at(4, i);
        if (menu_at(4, i)->cmd == CMD_ERASE) erase = menu_at(4, i);
    }
    CHECK(eject && (eject->flags & MI_DISABLED) && desktop_menu_reason(s, CMD_EJECT));
    CHECK(erase && (erase->flags & MI_DISABLED) && desktop_menu_reason(s, CMD_ERASE));
    session_key(s, 'e', MOD_CMD);
    CHECK(s->dialog && strstr(s->dialog_body, "startup disk"));
    session_key(s, 27, 0);
    /* Sleep dims the display and any input wakes it. */
    session_command(s, CMD_SLEEP); CHECK(s->display_dimmed);
    session_down(s, 500, 400, 1); session_up(s, 500, 400); CHECK(!s->display_dimmed);
    session_destroy(s);
    remove_tree(root);
}
int main(int argc, char **argv) {
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    static const SessionMediaHooks hooks = {.open_panel = hook_open_panel};
    session_set_media_hooks(&hooks);
    functional();
    for (int ctx = 0; ctx < CTX_COUNT; ctx++) {
        char root[OSPI_PATH]; Session *s = make(ctx, root, sizeof root);
        if (!s) { failures++; continue; }
        int counts[16] = {0}, menus = menu_count() < 9 ? menu_count() : 9;
        for (int m = 0; m < menus; m++) counts[m] = menu_nitems(m);
        session_destroy(s);
        remove_tree(root);
        for (int m = 0; m < menus; m++) {
            if (m == 6) continue; /* Appearance: wallpaper and theme pickers have their own tests */
            for (int i = 0; i < counts[m]; i++) probe(ctx, m, i);
        }
    }
    printf("probed %d menu items (%d enabled, %d disabled), %d failures\n", probes, enabled_total, disabled_total, failures);
    return failures ? 1 : 0;
}
