/* Format detection and recursive unwrapping into an archive listing.
 * Wrappers (BinHex, MacBinary, AppleSingle) and containers (ZIP,
 * StuffIt, Compact Pro, disk images holding HFS/HFS+) are recognized by
 * content, never by file name extension. Unwrapping continues while the
 * current object is a single wrapped file; an archive's members are not
 * expanded further. */
#ifndef OS999_UNPACK_H
#define OS999_UNPACK_H

#include "os999_storage.h"

typedef struct {
    char chain[256];     /* e.g. "BinHex > StuffIt" */
    char volume[256];    /* HFS volume name when a disk image was read */
    int external;        /* 1 when the external unar fallback was used */
} StUnpackInfo;

typedef enum {
    ST_FMT_UNKNOWN, ST_FMT_BINHEX, ST_FMT_MACBINARY, ST_FMT_APPLESINGLE, ST_FMT_APPLEDOUBLE,
    ST_FMT_ZIP, ST_FMT_SIT, ST_FMT_SIT5, ST_FMT_SITX, ST_FMT_CPT, ST_FMT_DC42, ST_FMT_NDIF,
    ST_FMT_UDIF, ST_FMT_HFS_IMAGE
} StFormat;

StFormat st_detect(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n);
const char *st_format_name(StFormat f);

/* Unpack in memory. `name` names a plain file result. rsrc may be NULL. */
int st_unpack(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n, const char *name,
              StArchive *out, StUnpackInfo *info, const StLimits *lim, StErr *err);
/* Read path (and an optional "<path>.rsrc" sidecar) and unpack it. With
 * allow_external = ST_EXT_UNSUPPORTED, run `unar` (no shell, private
 * temporary folder) when our decoders report ST_EUNSUPPORTED; with
 * ST_EXT_ARCHIVE, also when the file is not a format we recognize (the
 * caller expects an archive: RAR, 7z, LHA and so on go to unar). */
#define ST_EXT_NONE 0
#define ST_EXT_UNSUPPORTED 1
#define ST_EXT_ARCHIVE 2
int st_unpack_file(const char *path, int allow_external, StArchive *out, StUnpackInfo *info,
                   const StLimits *lim, StErr *err);
/* Import a host folder tree (data files + "<name>.rsrc" AppleDouble or
 * raw sidecars) as an archive. Symlinks are skipped. */
int st_import_tree(const char *dir, StArchive *out, const StLimits *lim, StErr *err);

#endif
