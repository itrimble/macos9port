#ifndef MAC_RESOURCE_INCLUDED
#define MAC_RESOURCE_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define MAC_RESOURCE_TYPE(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | \
     ((uint32_t)(uint8_t)(c) << 8) | (uint32_t)(uint8_t)(d))

/* Views borrow immutable bytes. Keep the source alive until all views expire.
 * Treat the fields of MacResourceFork as private after a successful open. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
    size_t data_offset, data_size;
    size_t map_offset, map_size;
    size_t types_offset, names_offset;
    size_t type_count, count;
} MacResourceFork;

typedef struct {
    uint32_t type;
    int16_t id;
    uint8_t attributes;
    const uint8_t *data;
    size_t size;
    const uint8_t *name; /* Raw MacRoman, not NUL-terminated; NULL if unnamed. */
    size_t name_size;
} MacResource;

/* 0: valid fork; -1: malformed input or invalid arguments. No allocation. */
int mac_resource_open(MacResourceFork *out, const void *bytes, size_t size,
                      char *err, size_t err_size);
/* Accept a raw fork or an AppleSingle/AppleDouble v2 resource entry (ID 2). */
int mac_resource_open_container(MacResourceFork *out, const void *bytes, size_t size,
                                char *err, size_t err_size);
size_t mac_resource_count(const MacResourceFork *fork);
/* 0: found; 1: not found; -1: invalid arguments. Output cleared on failure. */
int mac_resource_at(const MacResourceFork *fork, size_t index, MacResource *out);
int mac_resource_find(const MacResourceFork *fork, uint32_t type, int16_t id,
                      MacResource *out);

#endif
