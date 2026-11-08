#include "session_int.h"
#include "finder_files.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}
static uint64_t metadata(uint64_t hash, const struct stat *st) {
    hash = mix(hash, st->st_dev); hash = mix(hash, st->st_ino);
    hash = mix(hash, st->st_mode); hash = mix(hash, st->st_size);
    hash = mix(hash, st->st_mtime); hash = mix(hash, st->st_ctime);
#ifdef __APPLE__
    hash = mix(hash, st->st_mtimespec.tv_nsec); hash = mix(hash, st->st_ctimespec.tv_nsec);
#else
    hash = mix(hash, st->st_mtim.tv_nsec); hash = mix(hash, st->st_ctim.tv_nsec);
#endif
    return hash;
}
static int stamp(Session *s, const Window *w, uint64_t *value) {
    int fd = finder_directory_open(s->root, w->rel);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st)) { int error = errno; close(fd); errno = error; return -1; }
    uint64_t hash = metadata(UINT64_C(14695981039346656037), &st);
    for (int i = 0; i < w->nitems; i++) {
        const char *name = window_items(w)[i].name;
        for (const unsigned char *p = (const unsigned char *)name; *p; p++) hash = mix(hash, *p);
        hash = mix(hash, 0);
        hash = !fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) ? metadata(hash, &st) : mix(hash, 0);
        char fork[OSPI_NAME + 6]; snprintf(fork, sizeof fork, "%s.rsrc", name);
        hash = !fstatat(fd, fork, &st, AT_SYMLINK_NOFOLLOW) ? metadata(hash, &st) : mix(hash, 0);
    }
    close(fd); *value = hash; return 0;
}
void finder_refresh(Session *s) {
    if (!s || s->down || s->drag || session_overlay_active(s)) return;
    /* A disconnected volume must not discard its open window layout. */
    int root = finder_directory_open(s->root, "");
    if (root < 0) return;
    close(root);
    if (s->desktop.kind == WIN_FOLDER) {
        uint64_t current;
        Window *w = &s->desktop;
        if (!stamp(s, w, &current) && (!w->folder_stamp_valid || current != w->folder_stamp)) {
            if (!reload_folder_window(s, w) && !stamp(s, w, &current)) { w->folder_stamp = current; w->folder_stamp_valid = 1; }
        }
    }
    for (int i = 0; i < s->nwin;) {
        Window *w = &s->win[i];
        if (w->kind != WIN_FOLDER || w->search_results) { i++; continue; }
        uint64_t current;
        if (stamp(s, w, &current)) {
            int error = errno;
            if (w->rel[0] && (error == ENOENT || error == ENOTDIR || error == ELOOP)) {
                close_index(s, i); continue;
            }
            w->folder_stamp_valid = 0; i++; continue;
        }
        if (!w->folder_stamp_valid || current != w->folder_stamp) {
            if (reload_folder_window(s, w)) { i++; continue; }
            s->click_valid = 0;
            if (!stamp(s, w, &current)) { w->folder_stamp = current; w->folder_stamp_valid = 1; }
        }
        i++;
    }
    session_clamp(s);
}
