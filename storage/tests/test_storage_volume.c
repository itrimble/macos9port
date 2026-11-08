/* Guest volumes: containment, quotas, journaled saves with simulated
 * crashes at every durable step, rename/remove, install rollback. */
#include "st_test.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[256];

static void fresh(const char *tag) {
    snprintf(root, sizeof root, "%s/os999-vol-%ld-%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid(), tag);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", root, root);
    CHECK(system(cmd) == 0);
}

static int exists(const char *rel) {
    char p[600];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return lstat(p, &st) == 0;
}

static void test_basic(void) {
    fresh("basic");
    StVolume *v;
    StErr err;
    CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
    StMacFile f, g;
    st_make_file(&f, "Save Game", 2000, 500, 41);
    CHECK_RC(st_vol_write(v, "Games/Monkey Island/Save Game", &f, &err), ST_OK);
    CHECK(exists("Games/Monkey Island/Save Game") && exists("Games/Monkey Island/Save Game.rsrc"));
    CHECK_RC(st_vol_read(v, "Games/Monkey Island/Save Game", &g, NULL, &err), ST_OK);
    CHECK(st_same_file(&f, &g) && g.mtime == f.mtime && !strcmp(g.name, "Save Game"));
    st_macfile_free(&g);
    /* A plain file with no Mac metadata gets no sidecar. */
    StMacFile p;
    st_macfile_init(&p);
    st_bytes_append(&p.data, "hi", 2);
    CHECK_RC(st_vol_write(v, "plain.txt", &p, &err), ST_OK);
    CHECK(exists("plain.txt") && !exists("plain.txt.rsrc"));
    /* A guest file named like a sidecar is stored apart from sidecars. */
    CHECK_RC(st_vol_write(v, "Tool.rsrc", &f, &err), ST_OK);
    CHECK(exists("Tool.rsrc_") && exists("Tool.rsrc_.rsrc"));
    StVolumeEntry *ents;
    size_t n;
    CHECK_RC(st_vol_list(v, "", &ents, &n, &err), ST_OK);
    int saw_tool = 0, saw_side = 0;
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(ents[i].name, "Tool.rsrc")) saw_tool = ents[i].rsrc_size == 500;
        if (strstr(ents[i].name, ".rsrc") && strcmp(ents[i].name, "Tool.rsrc")) saw_side = 1;
        if (!strncmp(ents[i].name, ".os999", 6)) saw_side = 1;
    }
    CHECK(saw_tool && !saw_side && n == 3);
    free(ents);
    CHECK_RC(st_vol_rename(v, "plain.txt", "Games/notes.txt", &err), ST_OK);
    CHECK(!exists("plain.txt") && exists("Games/notes.txt"));
    CHECK_RC(st_vol_rename(v, "Games/Monkey Island/Save Game", "Games/notes.txt", &err), ST_EEXIST);
    CHECK_RC(st_vol_remove(v, "Games/Monkey Island/Save Game", &err), ST_OK);
    CHECK(!exists("Games/Monkey Island/Save Game.rsrc"));
    CHECK_RC(st_vol_remove(v, "Games/Monkey Island", &err), ST_OK);
    CHECK_RC(st_vol_remove(v, "Games", &err), ST_EIO); /* not empty */
    StVolumeUsage u;
    st_vol_usage(v, &u);
    CHECK(u.files == 2);
    /* A second process cannot open the same volume for writing. */
    StVolume *w;
    CHECK_RC(st_vol_open(root, NULL, &w, &err), ST_EIO);
    st_vol_close(v);
    StVolumeOptions ro = {0, 0, 1};
    CHECK_RC(st_vol_open(root, &ro, &w, &err), ST_OK);
    CHECK_RC(st_vol_write(w, "x", &f, &err), ST_EINVAL);
    st_vol_close(w);
    st_macfile_free(&f);
    st_macfile_free(&p);
}

static void test_containment(void) {
    fresh("contain");
    char outside[300], cmd[900];
    snprintf(outside, sizeof outside, "%s-outside", root);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'; mkdir -p '%s' && ln -s '%s' '%s/Link' && ln -s '%s/secret' '%s/Leaf' && echo s > '%s/secret'",
             outside, outside, outside, root, outside, root, outside);
    CHECK(system(cmd) == 0);
    StVolume *v;
    StErr err;
    CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
    StMacFile f, g;
    st_make_file(&f, "x", 10, 0, 1);
    CHECK_RC(st_vol_write(v, "Link/escape", &f, &err), ST_EPATH);
    CHECK_RC(st_vol_write(v, "Leaf", &f, &err), ST_EEXIST);
    CHECK(st_vol_read(v, "Leaf", &g, NULL, &err) != ST_OK);
    CHECK(st_vol_read(v, "Link/secret", &g, NULL, &err) != ST_OK);
    CHECK_RC(st_vol_write(v, "../escape", &f, &err), ST_EPATH);
    CHECK_RC(st_vol_write(v, "a/../../escape", &f, &err), ST_EPATH);
    CHECK_RC(st_vol_write(v, ".os999-journal", &f, &err), ST_EPATH);
    CHECK_RC(st_vol_write(v, "bad\nname", &f, &err), ST_EPATH);
    CHECK_RC(st_vol_write(v, "", &f, &err), ST_EPATH);
    snprintf(cmd, sizeof cmd, "%s/escape", outside);
    CHECK(access(cmd, F_OK) != 0);
    CHECK_RC(st_vol_rename(v, "Leaf", "Link/moved", &err), ST_EPATH);
    st_vol_close(v);
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", outside);
    CHECK(system(cmd) == 0);
    st_macfile_free(&f);
}

static void test_quota(void) {
    fresh("quota");
    StVolumeOptions o = {20000, 3, 0};
    StVolume *v;
    StErr err;
    CHECK_RC(st_vol_open(root, &o, &v, &err), ST_OK);
    StMacFile f;
    st_make_file(&f, "a", 9000, 0, 2);
    CHECK_RC(st_vol_write(v, "a", &f, &err), ST_OK);
    CHECK_RC(st_vol_write(v, "b", &f, &err), ST_OK);
    CHECK_RC(st_vol_write(v, "c", &f, &err), ST_EQUOTA);
    CHECK(!exists("c"));
    st_bytes_free(&f.data);
    st_fill(&f.data, 100, 3, 1);
    CHECK_RC(st_vol_write(v, "a", &f, &err), ST_OK); /* replacing frees space */
    CHECK_RC(st_vol_write(v, "c", &f, &err), ST_OK);
    CHECK_RC(st_vol_write(v, "d", &f, &err), ST_EQUOTA); /* file count */
    st_vol_close(v);
    st_macfile_free(&f);
}

static void test_crash_recovery(void) {
    for (int step = 1; step <= 4; step++) {
        char tag[16];
        snprintf(tag, sizeof tag, "crash%d", step);
        fresh(tag);
        StVolume *v;
        StErr err;
        CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
        StMacFile old, neu, got;
        st_make_file(&old, "Doc", 3000, 400, 50);
        st_make_file(&neu, "Doc", 5000, 900, 51);
        CHECK_RC(st_vol_write(v, "Folder/Doc", &old, &err), ST_OK);
        st_vol_crash_after(v, step);
        CHECK_RC(st_vol_write(v, "Folder/Doc", &neu, &err), ST_EIO);
        st_vol_close(v);
        CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
        StVolumeUsage u;
        st_vol_usage(v, &u);
        CHECK(u.recovered >= 1);
        CHECK_RC(st_vol_read(v, "Folder/Doc", &got, NULL, &err), ST_OK);
        /* Before the journal commits the old version survives; after, the new one. */
        CHECK(st_same_file(step <= 2 ? &old : &neu, &got));
        CHECK(!exists(".os999-journal"));
        StVolumeEntry *ents;
        size_t n;
        CHECK_RC(st_vol_list(v, "Folder", &ents, &n, &err), ST_OK);
        CHECK(n == 1);
        free(ents);
        char cmd[600];
        snprintf(cmd, sizeof cmd, "ls -a '%s/Folder' | grep -q os999-tmp", root);
        CHECK(system(cmd) != 0);
        st_vol_close(v);
        st_macfile_free(&old); st_macfile_free(&neu); st_macfile_free(&got);
    }
}

static void test_install(void) {
    fresh("install");
    StArchive a = {0};
    StMacFile d, f1, f2;
    st_macfile_init(&d);
    snprintf(d.name, sizeof d.name, "App");
    st_archive_add(&a, "App", 1, &d, NULL, NULL);
    st_make_file(&f1, "App", 100, 2000, 61);
    st_archive_add(&a, "App/App", 0, &f1, NULL, NULL);
    st_make_file(&f2, "Data", 50000, 0, 62);
    st_archive_add(&a, "App/Data", 0, &f2, NULL, NULL);
    StVolumeOptions o = {30000, 0, 0};
    StVolume *v;
    StErr err;
    CHECK_RC(st_vol_open(root, &o, &v, &err), ST_OK);
    CHECK_RC(st_vol_install(v, "Applications", &a, &err), ST_EQUOTA); /* refused up front */
    CHECK(!exists("Applications/App"));
    st_vol_close(v);
    o.quota_bytes = 0;
    CHECK_RC(st_vol_open(root, &o, &v, &err), ST_OK);
    CHECK_RC(st_vol_install(v, "Applications", &a, &err), ST_OK);
    CHECK(exists("Applications/App/App.rsrc") && exists("Applications/App/Data"));
    CHECK_RC(st_vol_install(v, "Applications", &a, &err), ST_EEXIST); /* no silent overwrite */
    /* A failure midway removes what this install created. */
    st_vol_crash_after(v, 3);
    CHECK(st_vol_install(v, "Other", &a, &err) != ST_OK);
    st_vol_close(v);
    CHECK_RC(st_vol_open(root, NULL, &v, &err), ST_OK);
    CHECK(!exists("Other/App/App"));
    st_vol_close(v);
    st_archive_free(&a);
}

int main(void) {
    test_basic();
    test_containment();
    test_quota();
    test_crash_recovery();
    test_install();
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s/os999-vol-%ld-*", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (long)getpid());
    if (system(cmd)) {}
    return st_test_done("test_storage_volume");
}
