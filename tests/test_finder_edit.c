/* Finder name editing and keyboard navigation, end to end through the
 * session API the desktop and phone hosts use. */
#include "desktop_assets.h"
#include "finder_edit.h"
#include "ospi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); failures++; } } while (0)

static char root[] = "/tmp/os999-finder-edit-XXXXXX";
static uint32_t now = 1000;

static void tick(Session *s, uint32_t ms) { now += ms; session_tick(s, now); }
static void click(Session *s, int x, int y, int clicks) {
    s->input_now_ms = now;
    session_down(s, x, y, clicks);
    session_up(s, x, y);
}
static int exists(const char *rel) {
    char p[OSPI_PATH * 2];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    struct stat st;
    return stat(p, &st) == 0;
}
static int find(const Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) if (strcmp(w->items[i].name, name) == 0) return i;
    return -1;
}
static int selected(const Window *w) {
    for (int i = 0; i < w->nitems; i++) if (w->items[i].selected) return i;
    return -1;
}
static Window *front(Session *s) { return &s->win[s->nwin - 1]; }

/* Special folders keep their icons under new names, and installs still find
 * Applications after it's renamed. */
static void rename_in_root(Session *s, const char *from, const char *to) {
    for (int i = 0; i < 20 && (s->nwin || s->dialog); i++) {
        if (s->dialog) { fprintf(stderr, "dialog: %s\n", s->dialog_body); session_key(s, '\r', 0); }
        else session_key(s, 'w', MOD_CMD);
    }
    s->desk_sel = 0;
    session_key(s, '\r', 0);
    Window *w = front(s);
    int p = find(w, from);
    for (int i = 0; i < w->nitems; i++) w->items[i].selected = i == p;
    session_key(s, '\r', 0);
    session_text(s, to);
    session_key(s, '\r', 0);
}

static void test_special(Session *s) {
    session_key(s, 27, 0);   /* close the contextual menu left open above */
    int before = volume_count(root, "");
    rename_in_root(s, "Documents", "Papers");
    Window *w = front(s);
    int p = find(w, "Papers");
    CHECK(p >= 0 && strcmp(w->items[p].special, "documents_folder") == 0);
    CHECK(strcmp(desktop_asset_name(&w->items[p], ""), "documents_folder") == 0);
    CHECK(exists("Papers/.os999-folder") && volume_count(root, "") == before);   /* the mark stays hidden */
    CHECK(volume_count(root, "Papers") == volume_count(root, "Papers"));
    rename_in_root(s, "Papers", "Music");                 /* still Documents, not Music */
    w = front(s);
    p = find(w, "Music");
    CHECK(p >= 0 && strcmp(desktop_asset_name(&w->items[p], ""), "documents_folder") == 0);
    char made[OSPI_NAME];
    CHECK(volume_mkdir_unique(root, "", "Stuff", made, sizeof made) == 0);
    rename_in_root(s, "Stuff", "Downloads");              /* a plain folder takes the name's icon */
    w = front(s);
    p = find(w, "Downloads");
    CHECK(p >= 0 && !w->items[p].special[0] && !exists("Downloads/.os999-folder"));
    CHECK(strcmp(desktop_asset_name(&w->items[p], ""), "downloads_folder") == 0);
    rename_in_root(s, "Untitled", "Untitled");            /* no such item: harmless */

    char rel[OSPI_PATH];
    CHECK(volume_special_folder(root, "applications_folder", "Applications", rel, sizeof rel) == 0 &&
          strcmp(rel, "Applications") == 0);
    rename_in_root(s, "Applications", "Apps");
    CHECK(exists("Apps/.os999-folder") && !exists("Applications"));
    CHECK(volume_special_folder(root, "applications_folder", "Applications", rel, sizeof rel) == 0 &&
          strcmp(rel, "Apps") == 0 && !exists("Applications"));
    rename_in_root(s, "Apps", "Applications");            /* back again: still the same folder */
    CHECK(volume_special_folder(root, "applications_folder", "Applications", rel, sizeof rel) == 0 &&
          strcmp(rel, "Applications") == 0);
    CHECK(volume_special_folder(root, "movies_folder", "Movies", rel, sizeof rel) == 0 &&
          strcmp(rel, "Movies") == 0 && exists("Movies"));   /* missing: made with the usual name */

    /* The richer Get Info retains the backup's standard-icon reset. */
    w = front(s); p = find(w, "Music");
    for (int i = 0; i < w->nitems; i++) window_items(w)[i].selected = i == p;
    session_command(s, CMD_GET_INFO);
    CHECK(s->dialog == 2 && s->dialog_cmd == CMD_STANDARD_ICON);
    session_key(s, 27, 0);
    CHECK(exists("Music/.os999-folder"));
    session_command(s, CMD_GET_INFO); session_key(s, '\r', 0);
    CHECK(!exists("Music/.os999-folder"));
    CHECK(volume_clear_folder_icon(root, "Music") == 0);
    CHECK(volume_clear_folder_icon(root, "../outside") == -1);
    char fitted[OSPI_NAME + 4];
    const char *long_name = "A very long folder name with accented R\xC3\xA9sum\xC3\xA9 characters";
    const char *fit = finder_fit_label(long_name, fitted, sizeof fitted);
    CHECK(fit == fitted && fb_text_width(fit) <= CELL_W - 8);
    CHECK(strstr(fit, "\xE2\x80\xA6") != NULL);

    /* Only well-formed marks count. */
    char icon[OSPI_SPECIAL];
    CHECK(volume_set_folder_icon(root, "Movies", "../../etc") == -1);
    CHECK(volume_set_folder_icon(root, "Movies", "Folder") == -1);
    char path[OSPI_PATH * 2];
    snprintf(path, sizeof path, "%s/Movies/.os999-folder", root);
    FILE *f = fopen(path, "w");
    if (f) { fputs("not an icon\n", f); fclose(f); }
    CHECK(!volume_folder_icon(root, "Movies", icon, sizeof icon));
    CHECK(volume_set_folder_icon(root, "Movies", "music_folder") == 0 &&
          volume_folder_icon(root, "Movies", icon, sizeof icon) && strcmp(icon, "music_folder") == 0);
}

int main(void) {
    CHECK(mkdtemp(root) != NULL);
    CHECK(volume_seed(root) == 0);
    Session *s = session_create(root);
    if (!s) return 1;
    tick(s, 0);

    /* ⌘N makes "untitled folder" and starts editing it, the whole name selected. */
    session_key(s, 'n', MOD_CMD);
    CHECK(s->nwin == 1 && session_editing(s));
    CHECK(strcmp(s->edit_buf, "untitled folder") == 0 && s->edit_anchor == 0 && s->edit_caret == 15);
    Rect box;
    CHECK(session_edit_rect(s, &box) && box.w > 20);
    session_key(s, 'p', 0);                  /* keys alone don't type: text does */
    CHECK(strcmp(s->edit_buf, "untitled folder") == 0);
    session_text(s, "Projects");
    CHECK(strcmp(s->edit_buf, "Projects") == 0 && s->edit_caret == 8);
    session_key(s, '\r', 0);
    CHECK(!session_editing(s) && exists("Projects") && !exists("untitled folder"));
    int p = find(front(s), "Projects");
    CHECK(p >= 0 && selected(front(s)) == p);

    /* Return edits the selected name; caret keys, Backspace and Delete. */
    session_key(s, '\r', 0);
    CHECK(session_editing(s) && s->nwin == 1);   /* Return no longer opens */
    session_key(s, FINDER_KEY_RIGHT, 0);       /* collapse the selection to the end */
    CHECK(s->edit_anchor == 8 && s->edit_caret == 8);
    session_text(s, " 2026");
    session_key(s, FINDER_KEY_HOME, 0);
    session_text(s, "My ");
    session_key(s, FINDER_KEY_END, 0);
    session_key(s, 8, 0);
    session_key(s, FINDER_KEY_LEFT, 0);
    session_key(s, 127, 0);
    CHECK(strcmp(s->edit_buf, "My Projects 20") == 0);
    session_key(s, 'a', MOD_CMD);
    session_key(s, 'c', MOD_CMD);
    CHECK(strcmp(s->clip, "My Projects 20") == 0 && session_take_clip_out(s) && !session_take_clip_out(s));
    session_key(s, 27, 0);                     /* Esc keeps the old name */
    CHECK(!session_editing(s) && exists("Projects"));

    /* Colons and slashes never go in; the Finder stops at 31 characters. */
    session_key(s, '\r', 0);
    session_text(s, "a:b/c");
    CHECK(strcmp(s->edit_buf, "abc") == 0);
    session_text(s, "0123456789012345678901234567890123456789");
    CHECK(strcmp(s->edit_buf, "abc") == 0);      /* too long as a whole: refused */
    session_key(s, 'a', MOD_CMD);
    for (int i = 0; i < 40; i++) session_text(s, "\xC3\xA9");   /* é, two bytes each */
    CHECK(s->edit_len == 62);
    session_key(s, 'a', MOD_CMD);
    session_text(s, "ab\xC3");                 /* a split character is dropped */
    CHECK(strcmp(s->edit_buf, "ab") == 0);
    session_text(s, "\xA9");                    /* so is a stray continuation byte */
    CHECK(strcmp(s->edit_buf, "ab") == 0);
    session_key(s, 27, 0);

    /* A name already in use is refused with the classic alert. */
    char made[OSPI_NAME];
    CHECK(volume_mkdir_unique(root, "", "Taken", made, sizeof made) == 0 && exists("Taken"));
    session_key(s, 'w', MOD_CMD);
    CHECK(s->nwin == 0);
    session_command(s, CMD_OPEN);                /* nothing selected on the desktop */
    s->desk_sel = 0;
    session_key(s, '\r', 0);                     /* the disk: Return still opens it */
    CHECK(s->nwin == 1 && !session_editing(s));
    p = find(front(s), "Projects");
    CHECK(p >= 0 && find(front(s), "Taken") >= 0);
    for (int i = 0; i < front(s)->nitems; i++) front(s)->items[i].selected = i == p;
    session_key(s, '\r', 0);
    session_text(s, "Taken");
    session_key(s, '\r', 0);
    CHECK(s->dialog && exists("Projects"));
    session_key(s, '\r', 0);
    CHECK(!s->dialog);

    /* Clicking elsewhere finishes the edit. */
    session_key(s, '\r', 0);
    session_text(s, "Work");
    click(s, 900, 700, 1);
    CHECK(!session_editing(s) && exists("Work") && !exists("Projects"));

    /* Click a selected name, wait out the double-click time: editing starts.
     * A double-click opens instead. */
    p = find(front(s), "Work");
    int cx = 0, cy = 0;
    CHECK(ospi_item_center(s, s->nwin - 1, p, &cx, &cy) == 0);
    click(s, cx, cy + 19, 1);                    /* the desktop click cleared it: select */
    tick(s, 600);
    CHECK(!session_editing(s));
    click(s, cx, cy + 19, 1);                    /* already selected: armed */
    tick(s, 200);
    CHECK(!session_editing(s));
    tick(s, 400);
    CHECK(session_editing(s) && strcmp(s->edit_old, "Work") == 0);
    session_key(s, 27, 0);
    click(s, cx, cy + 19, 1);
    tick(s, 100);
    click(s, cx, cy + 19, 2);                    /* a double-click opens the folder */
    tick(s, 700);
    CHECK(!session_editing(s) && s->nwin == 2 && strcmp(front(s)->rel, "Work") == 0);

    /* Renaming an open folder retitles its window. */
    session_command(s, CMD_NEW_FOLDER);        /* a subfolder, so a nested window exists */
    session_key(s, '\r', 0);
    CHECK(exists("Work/untitled folder"));
    session_rename_at(s, 0, 0);                  /* nothing there: no edit */
    CHECK(!session_editing(s));
    int root_win = s->win[0].rel[0] == 0 ? 0 : 1;
    p = find(&s->win[root_win], "Work");
    CHECK(ospi_item_center(s, root_win, p, &cx, &cy) == 0);
    Layout lay;
    window_layout(&s->win[root_win], &lay);
    if (rect_has(lay.outer, cx, cy + 19)) {
        click(s, lay.title.x + 40, lay.title.y + 5, 1);   /* bring it forward */
        root_win = s->nwin - 1;
        CHECK(ospi_item_center(s, root_win, p, &cx, &cy) == 0);
        CHECK(session_rename_at(s, cx, cy + 19) && session_editing(s));   /* Force Click on a name */
        session_text(s, "Studio");
        session_key(s, '\r', 0);
        int found = 0;
        for (int i = 0; i < s->nwin; i++)
            if (strcmp(s->win[i].rel, "Studio") == 0 && strcmp(s->win[i].title, "Studio") == 0) found = 1;
        CHECK(found && exists("Studio/untitled folder"));
    }

    /* Resource-fork sidecars travel with the file. */
    char path[OSPI_PATH * 2];
    snprintf(path, sizeof path, "%s/Notes", root);
    FILE *f = fopen(path, "w"); if (f) fclose(f);
    snprintf(path, sizeof path, "%s/Notes.rsrc", root);
    f = fopen(path, "w"); if (f) fclose(f);
    CHECK(volume_rename(root, "", "Notes", "Letters") == 0);
    CHECK(exists("Letters") && exists("Letters.rsrc") && !exists("Notes.rsrc"));
    CHECK(volume_rename(root, "", "Letters", ".hidden") == -1);
    CHECK(volume_rename(root, "", "Letters", "Trash") == -1);
    CHECK(volume_rename(root, "", "Letters", "x.rsrc") == -1);
    CHECK(volume_rename(root, "", "Letters", "Taken") == -2);
    CHECK(volume_rename(root, "", "Letters", "letters") == 0 && exists("letters"));

    /* Arrow keys walk the icons; typing selects by name. */
    for (int i = 0; i < 20 && s->nwin; i++) session_key(s, 'w', MOD_CMD);
    CHECK(!session_editing(s) && s->nwin == 0);
    /* Closing windows leaves the desktop without a selection. Start at Disk. */
    int disk_x, disk_y;
    CHECK(!ospi_desktop_center(s, 0, &disk_x, &disk_y));
    tick(s, 700); click(s, disk_x, disk_y, 1);
    CHECK(s->desk_sel == 0);
    session_key(s, FINDER_KEY_DOWN, 0);
    CHECK(s->desk_sel == 1);                     /* the Trash below the disk */
    session_key(s, FINDER_KEY_UP, 0);
    CHECK(s->desk_sel == 0);
    session_key(s, FINDER_KEY_DOWN, MOD_CMD);    /* ⌘↓ opens nothing without a window... */
    session_key(s, '\r', 0);
    CHECK(s->nwin == 1);
    Window *w = front(s);
    session_key(s, FINDER_KEY_RIGHT, 0);
    CHECK(selected(w) == 0);
    session_key(s, FINDER_KEY_RIGHT, 0);
    CHECK(selected(w) == 1);
    session_key(s, FINDER_KEY_LEFT, 0);
    CHECK(selected(w) == 0);
    tick(s, 2000);
    session_key(s, 's', 0);
    session_key(s, 't', 0);
    CHECK(strcmp(w->items[selected(w)].name, "Studio") == 0);
    tick(s, 2000);
    session_key(s, 't', 0);
    CHECK(strncasecmp(w->items[selected(w)].name, "t", 1) == 0);
    session_key(s, FINDER_KEY_DOWN, MOD_CMD);    /* ⌘↓ opens the selection */
    CHECK(s->nwin == 2);
    session_key(s, FINDER_KEY_UP, MOD_CMD);      /* ⌘↑ brings the enclosing folder forward */
    CHECK(front(s)->rel[0] == 0);

    /* Painting with an edit in progress, caret on and off. */
    static uint32_t px[OSPI_W * OSPI_H];
    session_key(s, '\r', 0);
    session_key(s, FINDER_KEY_END, 0);
    session_paint(s, px, OSPI_W, OSPI_H);
    CHECK(session_edit_rect(s, &box));
    CHECK(px[(box.y) * OSPI_W + box.x] == C_BLACK);   /* the box's frame */
    tick(s, 530);
    session_paint(s, px, OSPI_W, OSPI_H);
    session_context(s, 600, 600);                    /* a contextual click finishes too */
    CHECK(!session_editing(s));

    test_special(s);
    session_destroy(s);
    printf("finder edit: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
