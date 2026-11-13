#ifndef OS999_CLASSIC_APPS_H
#define OS999_CLASSIC_APPS_H
/* Install Classic Apps: fetch third-party classic software (QuickTime, TeachText, ...) into the
 * user's own OS999 volume. Nothing Apple-owned ships with OS999: the catalogue holds only names,
 * public Macintosh Garden page URLs and expected hashes. Nothing downloaded is ever executed.
 *
 * Catalogue text, one block per item (see classic_catalog_default_text()):
 *   [id]                 lowercase letters, digits, '-'
 *   name = Display name
 *   page = https://macintoshgarden.org/apps/...   (empty: local install only)
 *   file = archive leaf to pick on that page      (empty: the page's first download)
 *   md5 / sha256 = expected archive hash          (empty when not known)
 *   size = exact archive bytes (empty: unknown)   size_mb = size shown to the user
 *   dest = Applications | System Folder:Extensions    (guest-volume folder, ':' separated)
 *   wrap = yes|no        yes: expand into a new folder of the item's name inside dest
 *   default = yes|no     ticked in the dialog
 *   note = one line shown to the user */
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum { CLASSIC_MAX_APPS = 24 };

typedef struct {
    char id[32], name[56], page[256], file[96], md5[33], sha256[65], dest[64], size_mb[16], note[200];
    uint64_t size;
    int wrap, default_on;
} ClassicApp;

typedef struct { ClassicApp app[CLASSIC_MAX_APPS]; int n; } ClassicCatalog;

typedef enum {
    CA_OK = 0, CA_ALREADY, CA_CANCELLED, CA_FAILED, CA_NO_SOURCE, CA_HASH, CA_FULL
} ClassicResult;

/* Fetch the archive for `app` into the host file `dest`. Return 0, or -1 with a sentence in err. */
typedef int (*ClassicFetch)(const ClassicApp *app, const char *dest, const atomic_bool *cancel,
                            void *ctx, char *err, size_t err_n);
typedef void (*ClassicProgress)(void *ctx, const char *stage);

typedef struct {
    ClassicFetch fetch;              /* NULL: Macintosh Garden through the secure network layer */
    ClassicProgress progress;        /* may be NULL */
    void *ctx;
    const atomic_bool *cancel;       /* may be NULL; checked before each step */
} ClassicHooks;

/* Catalogue. Returns the number of items, or -1 with err describing the first bad line. */
int classic_catalog_parse(const char *text, ClassicCatalog *out, char *err, size_t err_n);
const char *classic_catalog_default_text(void);
int classic_catalog_default(ClassicCatalog *out);
const ClassicApp *classic_catalog_find(const ClassicCatalog *c, const char *id);

/* Hashes of a host file (lowercase hex). */
int classic_hash_file(const char *path, char md5_hex[33], char sha256_hex[65], uint64_t *size);

/* The https download URL on a Garden page for the file named `leaf` (whole name only), or -1. */
int classic_pick_named(const char *html, const char *leaf, char *out, size_t n);

/* 1 when a receipt for the item exists and the folder it names is still on the volume. */
int classic_installed(const char *root, const ClassicApp *app);
/* Volume-relative folder for a catalogue dest string ("Applications", "System Folder:Extensions"). */
int classic_dest_rel(const char *root, const char *dest, char *rel, size_t n);

/* Download (or fetch through hooks), verify, check capacity, expand into the volume, write a receipt
 * in <volume>/.os999/installs/. rel_out gets the volume-relative folder the files landed in. */
ClassicResult classic_install(const char *root, const ClassicApp *app, const ClassicHooks *hooks,
                              char *rel_out, size_t rel_n, char *err, size_t err_n);
/* Install from a host archive file or folder the user already has (host_path is absolute). */
ClassicResult classic_install_local(const char *root, const char *host_path, const ClassicHooks *hooks,
                                    char *rel_out, size_t rel_n, char *err, size_t err_n);

/* Dialog window (classic_apps_panel.c), drawn in OS999 inside a ":classicapps" text window. */
#include "draw.h"
#include "ospi.h"
int classic_apps_open(Session *s);
int classic_apps_first_run(Session *s);   /* opens the dialog once per volume, the first time the desktop starts */
int classic_apps_panel_paint(Fb *fb, const Window *w, const Layout *lay);   /* 1 when w is the dialog */
int classic_apps_panel_click(Session *s, Window *w, int x, int y);          /* 1 when handled */
int classic_apps_panel_key(Session *s, Window *w, int key, int mods);       /* 1 when used */
int classic_apps_panel_wheel(Window *w, int delta);                          /* 1 when used */
#endif
