#ifndef MAC_RESFORK_WRITE_H
#define MAC_RESFORK_WRITE_H
#include "mac_resource.h"

/* An editable copy of a resource fork and the writer that turns it back into bytes.
 * The layout written is the public resource-file layout: a 16-byte header (where the data and
 * the map are), a reserved area, the data area (a 4-byte length before each resource), then
 * the map (a copy of the header, the file attributes, a type list, one reference list per
 * type, and the names). Resources keep their order; new ones follow the last of their type. */
typedef struct {
    uint32_t type, size;
    int16_t id;
    uint8_t attrs;
    uint8_t *name;     /* Pascal string with its length byte, or NULL */
    uint8_t *data;     /* owned */
} MacWRes;
typedef struct {
    MacWRes *res;
    size_t count, cap;
    uint16_t map_attrs;      /* the file attributes word of the map */
    uint8_t reserved[240];   /* the system and application areas that follow the header, kept as found */
} MacWFork;

enum { MAC_WFORK_RESOURCES = 4096, MAC_WFORK_BYTES = 16 * 1024 * 1024 };

/* 0, or -108 (memory) / -199 (more resources than the Resource Manager here accepts). */
int mac_wfork_load(MacWFork *w, const MacResourceFork *fork);
void mac_wfork_init(MacWFork *w);   /* an empty fork */
void mac_wfork_free(MacWFork *w);
long mac_wfork_find(const MacWFork *w, uint32_t type, int16_t id);   /* index or -1 */
/* Add a resource (copies name and data). -194 if the type and ID exist or the limit is reached, -108 for memory. */
int mac_wfork_add(MacWFork *w, uint32_t type, int16_t id, uint8_t attrs, const uint8_t *pascal_name, const uint8_t *data, uint32_t size);
int mac_wfork_set_data(MacWFork *w, size_t index, const uint8_t *data, uint32_t size);
int mac_wfork_set_name(MacWFork *w, size_t index, const uint8_t *pascal_name);
void mac_wfork_remove(MacWFork *w, size_t index);
/* Serialise. 0 with *bytes (malloc'd) and *size, -199 when a limit of the format is passed, -108 for memory. */
int mac_wfork_build(const MacWFork *w, uint8_t **bytes, size_t *size);
#endif
