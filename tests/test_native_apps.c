#include "ospi.h"
#include "term.h"
#include "native_apps.h"
#include "notes.h"
#include "finder_recent.h"
#include "movie_window.h"
#include "movie_fixture.h"
#include "finder_drag.h"
#include "session_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); failures++; } } while (0)
static int recent(Session *s, int app) {
    for (int i = 0; i < s->nrecent; i++)
        if (s->recent[i].application && !strcmp(s->recent[i].path, native_catalog_path(app))) return i;
    return -1;
}
static int named(Window *w, const char *name) {
    for (int i = 0; i < w->nitems; i++) if (!strcmp(window_items(w)[i].name, name)) return i;
    return -1;
}
static void catalog_checks(Session *s) {
    CHECK(!open_folder(s, "Applications"));
    Window *apps = &s->win[s->nwin - 1];
    for (int app = NATIVE_TERMINAL; app <= NATIVE_QUAKE; app++) {
        const char *path = native_catalog_path(app);
        CHECK(native_catalog_id(s->root, path) == app);
        int i = named(apps, strrchr(path, '/') + 1);
        CHECK(i >= 0 && window_items(apps)[i].kind == ITEM_APPLICATION);
    }
    CHECK(named(apps, "Nothing Installed Yet") < 0);
    char note_path[OSPI_PATH]; snprintf(note_path, sizeof note_path, "%s/Documents/Notes", s->root);
    FILE *note = fopen(note_path, "wb"); CHECK(note);
    if (note) { for (int i = 0; i < 6000; i++) fputc('x', note); fclose(note); }
    int notes = named(apps, "Notes"); CHECK(notes >= 0); open_item(s, apps, notes);
    CHECK(s->win[s->nwin - 1].kind == WIN_TEXT && recent(s, NATIVE_NOTES) >= 0);
    CHECK(notes_active(s) && strlen(window_text(&s->win[s->nwin - 1])) == 6000);
    CHECK(!native_app_open(s, NATIVE_MOVIE));
    CHECK(s->win[s->nwin - 1].kind == WIN_MOVIE && !s->win[s->nwin - 1].movie);
    CHECK(recent(s, NATIVE_MOVIE) >= 0);
    MovieFixture clip; CHECK(!movie_fixture_build(&clip, 0, MAC_MOVIE_CODEC_RAW, 32));
    char path[OSPI_PATH]; snprintf(path, sizeof path, "%s/Documents/drop.mov", s->root);
    FILE *f = fopen(path, "wb"); CHECK(f); CHECK(fwrite(clip.data, 1, clip.len, f) == clip.len); fclose(f);
    CHECK(!open_folder(s, "Documents")); int docs = s->nwin - 1;
    Window *folder = &s->win[docs]; int item = named(folder, "drop.mov"); CHECK(item >= 0);
    select_only(folder, item);
    /* Raise the idle player, then drop the real movie into its content. */
    bring_front(s, docs - 1); docs--;
    Layout lay; window_layout(&s->win[s->nwin - 1], &lay); int count = s->nwin;
    finder_drag_drop(s, docs, lay.content.x + 15, lay.content.y + 15);
    CHECK(s->nwin == count && s->win[s->nwin - 1].kind == WIN_MOVIE && s->win[s->nwin - 1].movie);
    CHECK(access(path, F_OK) == 0);
    CHECK(!native_app_open(s, NATIVE_BROWSER) && s->browser_request);
    CHECK(!native_app_open(s, NATIVE_CONTROLS) && recent(s, NATIVE_CONTROLS) >= 0);
}

int main(void) {
    char root[] = "/tmp/os999-native-apps-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(!volume_seed(root));
    setenv("HOME", root, 1);
    char cli[OSPI_PATH], log[OSPI_PATH], shell_path[OSPI_PATH];
    snprintf(shell_path, sizeof shell_path, "%s/test-shell", root);
    FILE *shell_file = fopen(shell_path, "w"); CHECK(shell_file);
    fputs("#!/bin/sh\nexec /bin/sh\n", shell_file); fclose(shell_file);
    CHECK(!chmod(shell_path, 0700)); setenv("SHELL", shell_path, 1);
    snprintf(cli,sizeof cli,"%s/fake-tailscale",root);
    snprintf(log,sizeof log,"%s/cli.log",root);
    FILE *f=fopen(cli,"w"); CHECK(f);
    fputs("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$OS999_FAKE_CLI_LOG\"\ncase \"$*\" in\n'status --json') printf '%s\\n' '{\"BackendState\":\"Running\",\"Self\":{\"HostName\":\"fixture\"},\"Peer\":{}}';;\n*) exit 91;;\nesac\n",f);
    fclose(f); CHECK(!chmod(cli,0700));
    setenv("OS999_TAILSCALE_CLI",cli,1); setenv("OS999_FAKE_CLI_LOG",log,1);
    Session *s=session_create(root); CHECK(s); int before=s->nwin;
    CHECK(!terminal_tailnet_open(s)); CHECK(s->nwin==before+1);
    int found=0;
    for(int i=0;i<s->nrecent;i++) if(s->recent[i].application && strstr(s->recent[i].path,"Tailscale")) found=1;
    CHECK(found);
    CHECK(terminal_text_input(s,"quit")); CHECK(terminal_key(s,'\r',0));
    for(int i=0;i<500 && s->nwin>before;i++) { usleep(10000); terminal_tick(s); }
    CHECK(s->nwin==before); CHECK(!s->quit);
    f=fopen(log,"r"); CHECK(f);
    char calls[512]={0}; if(f) { fread(calls,1,sizeof calls-1,f); fclose(f); }
    CHECK(!strcmp(calls,"status --json\n"));
    CHECK(recent(s, NATIVE_TAILSCALE) >= 0);
    int index = recent(s, NATIVE_TAILSCALE);
    CHECK(finder_recent_command(s, CMD_RECENT_BASE + index));
    CHECK(terminal_front(s) && !strcmp(terminal_front(s)->title, "Tailscale"));
    close_index(s, s->nwin - 1);
    CHECK(!terminal_open(s, TERM_MODE_CONSOLE));
    CHECK(recent(s, NATIVE_CONSOLE) >= 0);
    before = s->nwin - 1;
    CHECK(terminal_text_input(s, "quit")); CHECK(terminal_key(s, '\r', 0)); terminal_tick(s);
    CHECK(s->nwin == before && !s->quit);
    char *shell[] = {"/bin/sh", NULL};
    CHECK(!terminal_open_custom(s, TERM_MODE_SHELL, shell));
    CHECK(terminal_text_input(s, "exit")); CHECK(terminal_key(s, '\r', 0));
    for (int i = 0; i < 500 && s->nwin > before; i++) { usleep(10000); terminal_tick(s); }
    CHECK(s->nwin == before);
    CHECK(!native_app_open(s, NATIVE_TERMINAL));
    CHECK(recent(s, NATIVE_TERMINAL) >= 0);
    CHECK(terminal_text_input(s, "exit")); CHECK(terminal_key(s, '\r', 0));
    for (int i = 0; i < 500 && s->nwin > before; i++) { usleep(10000); terminal_tick(s); }
    CHECK(s->nwin == before);
    char *success[] = {"/bin/sh", "-c", "exit 0", NULL};
    CHECK(!terminal_open_custom(s, TERM_MODE_SHELL, success));
    CHECK(!terminal_open_custom(s, TERM_MODE_SHELL, success));
    for (int i = 0; i < 500 && s->nwin > before; i++) { usleep(10000); terminal_tick(s); }
    CHECK(s->nwin == before); /* Removal must not skip the next exited window. */
    char *failed[] = {"/bin/sh", "-c", "exit 3", NULL};
    CHECK(!terminal_open_custom(s, TERM_MODE_SHELL, failed));
    for (int i = 0; i < 20; i++) { usleep(10000); terminal_tick(s); }
    CHECK(s->nwin == before + 1); /* Failure diagnostics remain available. */
    close_index(s, s->nwin - 1);
    catalog_checks(s);
    session_destroy(s);
    s = session_create(root); CHECK(s && recent(s, NATIVE_TAILSCALE) >= 0 && recent(s, NATIVE_MOVIE) >= 0);
    session_destroy(s);
    /* Seeding never overwrites a user's replacement; invalid markers cannot launch. */
    char notes_path[OSPI_PATH]; snprintf(notes_path, sizeof notes_path, "%s/Applications/Notes", root);
    f = fopen(notes_path, "w"); CHECK(f); fputs("User's own file", f); fclose(f);
    CHECK(!volume_seed(root) && !native_catalog_id(root, "Applications/Notes"));
    CHECK(!unlink(notes_path)); CHECK(!symlink("../Documents/Notes", notes_path));
    CHECK(!native_catalog_id(root, "Applications/Notes"));
    char bad_root[] = "/tmp/os999-native-note-failure-XXXXXX"; CHECK(mkdtemp(bad_root));
    CHECK(!volume_seed(bad_root));
    snprintf(notes_path, sizeof notes_path, "%s/Documents/Notes", bad_root);
    CHECK(!unlink(notes_path) && !symlink("../Read Me", notes_path));
    s = session_create(bad_root); CHECK(s);
    before = s->nwin;
    CHECK(native_app_open(s, NATIVE_NOTES) < 0);
    CHECK(s->nwin == before && recent(s, NATIVE_NOTES) < 0);
    session_destroy(s);
    unsetenv("OS999_TAILSCALE_CLI"); unsetenv("OS999_FAKE_CLI_LOG");
    printf("Native application regression: %d failures; fixture %s\n",failures,root);
    return failures?1:0;
}
