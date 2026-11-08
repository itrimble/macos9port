/* OS999 storage: classic Macintosh containers, archives, disk images,
 * HFS/HFS+ and writable guest volumes. C11, no SDL, no global state.
 * Every parser treats its input as hostile: bounds are checked before
 * each read, sizes are capped by StLimits, and errors carry a message. */
#ifndef OS999_STORAGE_H
#define OS999_STORAGE_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ST_OK = 0,
    ST_EFORMAT = -1,      /* malformed or truncated input */
    ST_EUNSUPPORTED = -2, /* valid input using a feature we do not decode */
    ST_ELIMIT = -3,       /* exceeds a size, count or depth limit */
    ST_ECHECKSUM = -4,    /* CRC or checksum mismatch */
    ST_EIO = -5,          /* host I/O failure */
    ST_ENOMEM = -6,
    ST_EPATH = -7,        /* unsafe or invalid path */
    ST_EQUOTA = -8,       /* guest volume quota exceeded */
    ST_EEXIST = -9,
    ST_ENOENT = -10,
    ST_EINVAL = -11
} StStatus;

typedef struct {
    int code;
    char msg[192];
} StErr;

typedef struct {
    uint8_t *p;
    size_t n, cap;
} StBytes;

typedef struct {
    uint64_t max_total;    /* total decoded bytes across an operation */
    uint64_t max_entry;    /* one fork */
    uint32_t max_entries;  /* files + folders */
    uint32_t max_depth;    /* folder nesting and recursive unwrapping */
} StLimits;

/* Mac dates are seconds since 1904-01-01 00:00 local time, as stored. */
typedef struct {
    char name[256];        /* UTF-8; may contain '/', never NUL */
    uint8_t finfo[16];     /* FInfo: type, creator, flags, location, folder */
    uint8_t fxinfo[16];    /* FXInfo */
    uint32_t ctime, mtime;
    StBytes data, rsrc;
} StMacFile;

typedef struct {
    char *path;            /* UTF-8, components joined with '/', host-safe */
    int is_dir;
    StMacFile file;        /* name = last component in Mac form */
} StEntry;

typedef struct {
    StEntry *v;
    size_t n, cap;
    uint64_t total;
} StArchive;

/* ---- common ---- */
const char *st_status_text(int code);
StLimits st_limits_default(void);
int st_fail(StErr *err, int code, const char *fmt, ...);
void st_bytes_free(StBytes *b);
int st_bytes_append(StBytes *b, const void *src, size_t n);
int st_bytes_reserve(StBytes *b, size_t n);
int st_read_file(const char *path, uint64_t max, StBytes *out, StErr *err);
void st_macfile_init(StMacFile *f);
void st_macfile_free(StMacFile *f);
uint32_t st_mac_type(const StMacFile *f);
uint32_t st_mac_creator(const StMacFile *f);
uint16_t st_mac_flags(const StMacFile *f);
void st_mac_set_type_creator(StMacFile *f, uint32_t type, uint32_t creator);
void st_archive_free(StArchive *a);
/* Takes ownership of *file's forks on success. path is a '/' joined
 * sequence of Mac names; each component is sanitized for the host. */
int st_archive_add(StArchive *a, const char *path, int is_dir, StMacFile *file,
                   const StLimits *lim, StErr *err);

/* MacRoman <-> UTF-8. Return bytes written (excluding NUL) or -1. */
int st_macroman_to_utf8(const uint8_t *src, size_t n, char *out, size_t cap);
int st_utf8_to_macroman(const char *src, uint8_t *out, size_t cap);
/* One path component made safe for a host filesystem: no '/', NUL, or
 * controls, never "." or "..", at most 255 bytes. '/' becomes ':'. */
int st_host_component(const char *mac_name, char *out, size_t cap);

uint16_t st_crc16_xmodem(uint16_t crc, const uint8_t *p, size_t n);
uint16_t st_crc16_arc(uint16_t crc, const uint8_t *p, size_t n);
uint32_t st_crc32(uint32_t crc, const uint8_t *p, size_t n);

/* ---- single-file encodings (L02) ---- */
int st_macbinary_detect(const uint8_t *p, size_t n); /* 0 none, 1, 2, 3 = version */
int st_macbinary_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err);
int st_macbinary_encode(const StMacFile *f, StBytes *out, StErr *err); /* MacBinary III */
int st_binhex_detect(const uint8_t *p, size_t n);
int st_binhex_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err);
int st_binhex_encode(const StMacFile *f, StBytes *out, StErr *err);
int st_applesingle_detect(const uint8_t *p, size_t n); /* 0, 1 single, 2 double */
/* For AppleDouble, out->data is left empty; merge with the data file. */
int st_applesingle_decode(const uint8_t *p, size_t n, StMacFile *out, const StLimits *lim, StErr *err);
int st_applesingle_encode(const StMacFile *f, int is_double, StBytes *out, StErr *err);

/* ---- archives (L02) ---- */
int st_zip_detect(const uint8_t *p, size_t n);
int st_zip_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err);
int st_sit_detect(const uint8_t *p, size_t n); /* 0 none, 1 classic, 5 SIT5, 10 sitx */
int st_sit_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err);
int st_cpt_detect(const uint8_t *p, size_t n);
int st_cpt_decode(const uint8_t *p, size_t n, StArchive *out, const StLimits *lim, StErr *err);

/* ---- disk images (L03) ---- */
typedef enum {
    ST_IMG_NONE, ST_IMG_RAW, ST_IMG_DC42, ST_IMG_NDIF, ST_IMG_UDIF
} StImageKind;
StImageKind st_image_detect(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n);
/* Produce the raw device bytes. rsrc is required for NDIF (bcem resource). */
int st_image_decode(const uint8_t *p, size_t n, const uint8_t *rsrc, size_t rsrc_n,
                    StBytes *raw, const StLimits *lim, StErr *err);
int st_dc42_encode(const uint8_t *raw, size_t n, const char *name, StBytes *out, StErr *err);
int st_adc_decode(const uint8_t *p, size_t n, StBytes *out, size_t want, StErr *err);

/* Locate an HFS/HFS+ volume in raw device bytes (bare volume, Apple
 * partition map, or HFS wrapper). Returns 0 and sets offset/length. */
int st_find_hfs(const uint8_t *raw, size_t n, size_t *off, size_t *len, StErr *err);

/* ---- HFS / HFS+ (L03) ---- */
/* Read every file and folder of the volume into out. */
int st_hfs_read(const uint8_t *vol, size_t n, StArchive *out, char *vol_name, size_t vol_cap,
                const StLimits *lim, StErr *err);
/* Build a fresh HFS volume (400K..32M) holding the archive's entries. */
int st_hfs_write(const StArchive *a, const char *vol_name, size_t bytes, StBytes *out, StErr *err);

#endif
