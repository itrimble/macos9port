#ifndef MAC_FILES_INT_H
#define MAC_FILES_INT_H
/* Private state shared by the read-only File Manager service (mac_files.c)
 * and the writable save overlay (mac_files_write.c). Not a public API. */
#include "mac_runtime.h"
#include <dirent.h>
#include <stdint.h>

enum { FILE_LIMIT = 32, REF_BASE = 128, PB_BYTES = 50, FILE_NAME_MAX = 31 };

/* An open fork. Writable refs hold a hidden staging copy in the save
 * directory; PBClose commits it over the named file with one rename. */
typedef struct {
    int fd;
    int32_t mark;
    int writable;
    char name[FILE_NAME_MAX + 1];
} GuestFile;

typedef struct {
    int directory;         /* Pinned read-only application data root. */
    int saves;             /* Pinned writable overlay root, or -1 (read-only volume). */
    unsigned max_files;    /* Overlay quotas; zero means no overlay. */
    uint64_t max_bytes;
    GuestFile files[FILE_LIMIT];
} FileState;

/* Classification of a name across the single guest namespace. */
enum { FILE_NONE = 0, FILE_SAVE = 1, FILE_DATA = 2, FILE_SAVE_UNSUPPORTED = 3, FILE_DATA_UNSUPPORTED = 4 };

int mac_files_valid(MacCpu *cpu, uint32_t address, uint32_t length);
/* Copy a guest Pascal string into out (NUL terminated). Returns -50 when the
 * pointer, length or characters are unacceptable for any volume operation. */
int mac_files_read_name(MacCpu *cpu, uint32_t pointer, char *out, size_t capacity);
/* Length of a leading "OS Pi Data:" (this volume's name, any case) when a file name follows it, else 0. */
unsigned mac_files_volume_prefix(const char *name, size_t length);
int mac_files_save_name_ok(const char *name);
int mac_files_classify(const FileState *s, const char *name);
GuestFile *mac_files_open_by_name(FileState *s, const char *name);
/* Shared helpers (mac_files_write.c): ioVRefNum must name the data volume;
 * open a regular file by pinned directory without following links (returns
 * a descriptor or a File Manager error); Finder info in host metadata;
 * lock/absence error for a classified name; a DIR over a pinned descriptor. */
int mac_files_volume_ok(MacCpu *cpu, uint32_t pb);
int mac_files_open_regular(int directory, const char *name, int flags);
int mac_files_finder_get(int fd, uint8_t out[16]);
int mac_files_finder_set(int fd, const uint8_t in[16]);
int mac_files_locked_error(const FileState *s, int kind);
DIR *mac_files_open_listing(int directory);

/* Overlay entry points used by the dispatcher. open: 0 opened into *file,
 * 1 not an overlay file (continue with the data volume), <0 File Manager error. */
int mac_files_open_save(MacRuntime *rt, const char *name, unsigned permission, GuestFile *file);
int mac_files_close(FileState *s, GuestFile *file);
void mac_files_abort(FileState *s, GuestFile *file);
int mac_files_overlay_count(const FileState *s, unsigned *count);
int mac_files_create(MacRuntime *rt, uint32_t pb);
int mac_files_delete(MacRuntime *rt, uint32_t pb, int hfs);
int mac_files_rename(MacRuntime *rt, uint32_t pb);
/* Name-based cores (host names already validated for characters; State is mounted). create_name: quota,
 * duplicate and name rules; delete_name and rename_names: lock, busy and absence rules. open_name: PBOpen. */
int mac_files_create_name(MacRuntime *rt, const char *name);
int mac_files_delete_name(MacRuntime *rt, const char *name);
int mac_files_rename_names(MacRuntime *rt, const char *from, const char *to);
int mac_files_open_name(MacRuntime *rt, const char *name, unsigned permission, int *ref);
int mac_files_write(MacRuntime *rt, uint32_t pb, GuestFile *f);
int mac_files_set_eof(MacRuntime *rt, uint32_t pb, GuestFile *f);
int mac_files_set_fpos(MacRuntime *rt, uint32_t pb, GuestFile *f, int query);
int mac_files_get_info(MacRuntime *rt, uint32_t pb);
int mac_files_hget_info(MacRuntime *rt, uint32_t pb);
int mac_files_set_info(MacRuntime *rt, uint32_t pb);
int mac_files_set_vol(MacRuntime *rt, uint32_t pb);
#endif
