#ifndef MAC_FILES_INT_H
#define MAC_FILES_INT_H
/* Private state shared by the read-only File Manager service (mac_files.c)
 * and the writable save overlay (mac_files_write.c). Not a public API. */
#include "mac_runtime.h"
#include "volume_capacity.h"
#include <dirent.h>
#include <stdint.h>

/* A save-namespace name that stands for a file in a folder ("d<id>-name", see mac_fsspec.c): never listed as a root file. */
static inline int mac_files_in_folder(const char *n) {
    if (n[0] != 'd' || n[1] < '0' || n[1] > '9') return 0;
    const char *p = n + 1; while (*p >= '0' && *p <= '9') p++;
    return *p == '-' && p - n <= 3;
}
/* Folder IDs of the approximated folder model (mac_files_dircreate.c, mac_fsspec.c). */
enum { FOLDER_ID_FIRST = 3, FOLDER_ID_LAST = 66 };
uint32_t mac_files_folder_id(const char *name);          /* the ID a folder of that name gets */
int mac_files_dir_create(MacRuntime *rt, uint32_t pb);   /* PBDirCreate; the OSErr */
uint32_t mac_files_wd_folder(int vref);                  /* the folder ID a working-directory reference stands for, or 0 */
int mac_files_wd_ours(const MacRuntime *rt, unsigned selector, uint32_t dir_id, int vref);
int mac_files_wd(MacRuntime *rt, uint32_t pb, unsigned selector);   /* PBOpenWD (1), PBCloseWD (2), PBGetWDInfo (7) of a made folder */
int mac_files_pb_name(MacRuntime *rt, uint32_t pb, uint32_t name_ptr, char *out, size_t capacity);   /* ioNamePtr as a host name (folder prefix for a WD reference) */
int mac_files_folder_info(MacRuntime *rt, uint32_t pb);  /* PBGetCatInfo by name of a made folder: 1 and a directory record, else 0 */
enum { FILE_LIMIT = 32, REF_BASE = 128, PB_BYTES = 50, FILE_NAME_MAX = 31 };

/* An open fork. Writable refs hold a hidden staging copy in the save
 * directory; PBClose commits it over the named file with one rename. */
typedef struct {
    int fd;
    int32_t mark;
    int writable;
    char name[FILE_NAME_MAX + 1];
    int64_t flushed_ns;    /* modification time (ns) of the staging copy at the last flush (mac_files_flush.c) */
} GuestFile;

typedef struct {
    int directory;         /* Pinned read-only application data root. */
    int saves;             /* Pinned writable overlay root, or -1 (read-only volume). */
    unsigned max_files;    /* Overlay quotas; zero means no overlay. */
    uint64_t max_bytes;
    VolumeBudget disk;     /* internal-disk capacity budget (active after mac_files_disk) */
    GuestFile files[FILE_LIMIT];
} FileState;

/* Classification of a name across the single guest namespace. */
enum { FILE_NONE = 0, FILE_SAVE = 1, FILE_DATA = 2, FILE_SAVE_UNSUPPORTED = 3, FILE_DATA_UNSUPPORTED = 4 };

int mac_files_valid(MacCpu *cpu, uint32_t address, uint32_t length);
/* Copy a guest Pascal string into out (NUL terminated). Returns -50 when the
 * pointer, length or characters are unacceptable for any volume operation. */
int mac_files_read_name(MacCpu *cpu, uint32_t pointer, char *out, size_t capacity);
/* Length of a leading "OS999 HD:" (this volume's name, any case) when a file name follows it, else 0. */
unsigned mac_files_volume_prefix(const char *name, size_t length);
int mac_files_save_name_ok(const char *name);
int mac_files_classify(const FileState *s, const char *name);
GuestFile *mac_files_open_by_name(FileState *s, const char *name);
/* Shared helpers (mac_files_write.c): ioVRefNum must name the data volume;
 * open a regular file by pinned directory without following links (returns
 * a descriptor or a File Manager error); Finder info in host metadata;
 * lock/absence error for a classified name; a DIR over a pinned descriptor. */
int mac_files_volume_ok(MacCpu *cpu, uint32_t pb);
int mac_files_volume_ok_rt(MacRuntime *rt, uint32_t pb);   /* as above, and vRefNum -2 when the runtime has its System volume: its folders are this flat namespace (mac_files_dircreate.c) */
int mac_files_open_regular(int directory, const char *name, int flags);
int mac_files_finder_get(int fd, uint8_t out[16]);
int mac_files_finder_set(int fd, const uint8_t in[16]);
int mac_files_locked_error(const FileState *s, int kind);
DIR *mac_files_open_listing(int directory);

/* Overlay entry points used by the dispatcher. open: 0 opened into *file,
 * 1 not an overlay file (continue with the data volume), <0 File Manager error. */
int mac_files_open_save(MacRuntime *rt, const char *name, unsigned permission, GuestFile *file);
int mac_files_close(FileState *s, GuestFile *file);
int mac_files_stage_name(const char *name, char *out, size_t capacity);   /* ".stage-<name>" */
int mac_files_flush_staged(FileState *s);                                 /* commit dirty staging copies and re-stage them (mac_files_flush.c) */
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
int mac_files_get_volume(MacRuntime *rt, uint32_t pb);
int mac_files_set_vol(MacRuntime *rt, uint32_t pb);
int mac_files_hvol(MacRuntime *rt, uint32_t pb, int get); /* PBHGetVol / PBHSetVol (mac_files_hvol.c) */
int mac_files_open_rf(MacRuntime *rt, uint32_t pb);
int mac_files_crf_service(MacRuntime *rt, uint16_t opcode); /* CreateResFile $A9B1 (mac_files_crf.c) */
void mac_files_empty_fork(uint8_t out[286]); /* PBOpenRF / PBHOpenRF (mac_files_rf.c) */
int mac_files_write(MacRuntime *rt, uint32_t pb, GuestFile *f);
int mac_files_set_eof(MacRuntime *rt, uint32_t pb, GuestFile *f);
int mac_files_set_fpos(MacRuntime *rt, uint32_t pb, GuestFile *f, int query);
int mac_files_get_volparms(MacRuntime *rt, uint32_t pb);   /* PBHGetVolParms (mac_files_volparms.c) */
int mac_files_get_info(MacRuntime *rt, uint32_t pb);
int mac_files_hget_info(MacRuntime *rt, uint32_t pb);
int mac_files_set_info(MacRuntime *rt, uint32_t pb);
int mac_files_set_vol(MacRuntime *rt, uint32_t pb);
#endif
