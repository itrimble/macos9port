#ifndef MAC_FILES_INT_H
#define MAC_FILES_INT_H
/* Private state shared by the read-only File Manager service (mac_files.c)
 * and the writable save overlay (mac_files_write.c). Not a public API. */
#include "macroman.h"
#include "mac_runtime.h"
#include "volume_capacity.h"
#include <dirent.h>
#include <stdint.h>

#include "mac_dirs.h"

/* Names inside the File Manager service are host-relative paths ("Textures/oak"): see mac_dirs.h. A directory ID is the table mapping of such a path.
 * Directory-level entry points (mac_files_dirs.c, mac_catinfo.c): */
int mac_files_dir_create(MacRuntime *rt, uint32_t pb);   /* PBDirCreate; the OSErr */
int mac_files_folder_find(MacRuntime *rt, const char *name, int create, uint32_t *id);   /* a special folder by ':' path from the root (FindFolder), optionally making it and its parents; the OSErr */
uint32_t mac_files_wd_folder(const MacRuntime *rt, int vref);   /* the directory ID an open working-directory reference stands for, or 0 */
int mac_files_wd_ours(const MacRuntime *rt, unsigned selector, uint32_t dir_id, int vref);
int mac_files_wd(MacRuntime *rt, uint32_t pb, unsigned selector);   /* PBOpenWD (1), PBCloseWD (2), PBGetWDInfo (7) */
int mac_files_cat_info(MacRuntime *rt, uint32_t pb);     /* PBGetCatInfo (HFSDispatch selector 9): files and directories (mac_catinfo.c) */
int mac_files_fcb_info(MacRuntime *rt, uint32_t pb);     /* PBGetFCBInfo (HFSDispatch selector 8): the name and parent directory of an open file (mac_catinfo.c) */
int mac_files_fcb_ours(MacRuntime *rt);                  /* whether the call in A0 names a file this service holds, or the application's own file */
void mac_files_set_app(MacRuntime *rt, const char *name);   /* the application's own file name, reported for its resource file */
int mac_files_cat_move(MacRuntime *rt, uint32_t pb);     /* PBCatMove (HFSDispatch selector 5) */
/* Resolve a name against a volume/directory pair (mac_dirs_name.c): 0 and the host-relative path of the named object ("" is the root), or -35, -37, -120. */
int mac_dirs_resolve(MacRuntime *rt, int vref, uint32_t dir_id, const char *name, char out[DIRS_PATH_MAX]);
enum { FILE_LIMIT = 32, REF_BASE = 128, PB_BYTES = 50, FILE_NAME_MAX = 31 };

/* An open fork. Writable refs hold a hidden staging copy in the save
 * directory; PBClose commits it over the named file with one rename. */
typedef struct {
    int fd;
    int32_t mark;
    int writable;
    char name[DIRS_PATH_MAX];   /* host-relative path of the committed file */
    int64_t flushed_ns;    /* modification time (ns) of the staging copy at the last flush (mac_files_flush.c) */
} GuestFile;

typedef struct {
    int directory;         /* Pinned read-only application data root. */
    int saves;             /* Pinned writable overlay root, or -1 (read-only volume). */
    unsigned max_files;    /* Overlay quotas; zero means no overlay. */
    uint64_t max_bytes;
    VolumeBudget disk;     /* internal-disk capacity budget (active after mac_files_disk) */
    int hfs_call;          /* the call being served is an HFS (H-prefixed) flavour: ioDirID at +48 is an input (mac_files.c) */
    char app_name[MR_HOST_MAX];   /* the application file the runtime was started from (for PBGetFCBInfo on its resource file) */
    DirTable dirs;         /* directory IDs, working directories and the default directory (mac_dirs.h) */
    GuestFile files[FILE_LIMIT];
} FileState;

/* The host (UTF-8) spelling of a guest leaf name given as Mac Roman bytes, in directory `dir`: an existing entry's own spelling, else precomposed UTF-8. 0 or -37. */
int mac_dirs_leaf_to_host(const FileState *s, const char *dir, const uint8_t *mr, size_t n, char out[MR_HOST_MAX]);

/* Classification of a name across the single guest namespace. */
enum { FILE_NONE = 0, FILE_SAVE = 1, FILE_DATA = 2, FILE_SAVE_UNSUPPORTED = 3, FILE_DATA_UNSUPPORTED = 4 };

int mac_dirs_is_dir(const FileState *s, const char *rel);   /* a directory in the save overlay or the application data (and not shadowed by a save file) */
int mac_files_pb_path(MacRuntime *rt, uint32_t pb, uint32_t name_ptr, char *out, int allow_empty);   /* ioNamePtr + ioVRefNum (+ ioDirID for HFS calls) -> relative path; -50, -37, -35, -120 */
int mac_files_ptr_path(MacRuntime *rt, uint32_t name_ptr, int vref, uint32_t dir_id, char *out);   /* a bare Pascal-string name against a volume/directory (0, 0 = the default directory) */
int mac_files_ptr_path_raw(MacRuntime *rt, uint32_t name_ptr, int vref, uint32_t dir_id, char *out);   /* as above, but -37 (bdNamErr) for a bad name */
int mac_dirs_ensure_overlay(FileState *s, const char *dirpath);   /* make the overlay copy of a directory that exists (in data or overlay); -120 if it does not */
int mac_files_listing_dir(const FileState *s, const char *dirpath, int with_dirs, int hide_sidecars, char (**names)[256], unsigned *count);   /* sorted; a directory name ends in '/' */
int mac_files_fill_info(MacRuntime *rt, uint32_t pb, const char *path, int kind);   /* the file record of PBGetFInfo/PBGetCatInfo, bytes 30..79 */
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
int mac_files_usage(const FileState *s, unsigned *files, uint64_t *bytes);   /* save files and bytes across the overlay tree */
int mac_files_overlay_count(const FileState *s, unsigned *count);
int mac_files_set_cat_info(MacRuntime *rt, uint32_t pb);   /* PBSetCatInfo (mac_catinfo_set.c) */
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
int mac_files_open_rf_path(MacRuntime *rt, const char *name, unsigned permission, int *ref);   /* the resource fork of a file by path: its ".rsrc" sidecar */
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
