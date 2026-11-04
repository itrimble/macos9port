#include "mac_resource.h"

#include <stdio.h>
#include <string.h>

/* Independent implementation from public format descriptions, not Apple code.
 * Format reference: https://ciderpress2.com/formatdoc/ResourceFork-notes.html */
static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static size_t entries(const uint8_t *p) {
    uint16_t value = be16(p);
    return value == UINT16_MAX ? 0 : (size_t)value + 1;
}

static int within(size_t offset, size_t length, size_t total) {
    return offset <= total && length <= total - offset;
}

static int fail(char *err, size_t size, const char *message) {
    if (err && size) {
        snprintf(err, size, "%s", message);
    }
    return -1;
}

static int read_resource(const MacResourceFork *f, const uint8_t *type,
                         const uint8_t *ref, MacResource *out) {
    size_t data = ((size_t)ref[5] << 16) | ((size_t)ref[6] << 8) | ref[7];
    uint16_t name = be16(ref + 2);
    if (!within(data, 4, f->data_size)) {
        return -1;
    }
    const uint8_t *payload = f->bytes + f->data_offset + data;
    size_t length = be32(payload);
    if (!within(data + 4, length, f->data_size)) {
        return -1;
    }
    const uint8_t *name_bytes = NULL;
    size_t name_size = 0;
    if (name != UINT16_MAX) {
        size_t names_size = f->map_size - f->names_offset;
        if (!within(name, 1, names_size)) {
            return -1;
        }
        const uint8_t *p = f->bytes + f->map_offset + f->names_offset + name;
        name_size = p[0];
        if (!within((size_t)name + 1, name_size, names_size)) {
            return -1;
        }
        name_bytes = p + 1;
    }
    if (out) {
        uint16_t raw_id = be16(ref);
        out->type = be32(type);
        out->id = (int16_t)(raw_id <= INT16_MAX ? (int32_t)raw_id : (int32_t)raw_id - 65536);
        out->attributes = ref[4];
        out->data = payload + 4;
        out->size = length;
        out->name = name_bytes;
        out->name_size = name_size;
    }
    return 0;
}

int mac_resource_open(MacResourceFork *out, const void *bytes, size_t size,
                      char *err, size_t err_size) {
    MacResourceFork f = {0};
    if (err && err_size) {
        err[0] = 0;
    }
    if (!out) {
        return fail(err, err_size, "Missing resource-fork output.");
    }
    memset(out, 0, sizeof *out);
    if (!bytes || size < 16) {
        return fail(err, err_size, "Truncated resource-fork header.");
    }
    f.bytes = bytes;
    f.size = size;
    f.data_offset = be32(f.bytes);
    f.map_offset = be32(f.bytes + 4);
    f.data_size = be32(f.bytes + 8);
    f.map_size = be32(f.bytes + 12);
    if (f.data_offset < 16 || f.map_offset < 16 ||
        !within(f.data_offset, f.data_size, size) ||
        !within(f.map_offset, f.map_size, size) || f.map_size < 28) {
        return fail(err, err_size, "Invalid resource data/map bounds.");
    }
    if (f.data_size && f.data_offset < f.map_offset + f.map_size &&
        f.map_offset < f.data_offset + f.data_size) {
        return fail(err, err_size, "Resource data and map overlap.");
    }
    const uint8_t *map = f.bytes + f.map_offset;
    f.types_offset = be16(map + 24);
    f.names_offset = be16(map + 26);
    if (f.types_offset < 28 || !within(f.types_offset, 2, f.map_size) ||
        f.names_offset < f.types_offset + 2 || f.names_offset > f.map_size) {
        return fail(err, err_size, "Invalid resource type/name list offsets.");
    }
    const uint8_t *types = map + f.types_offset;
    f.type_count = entries(types);
    size_t type_bytes = 2 + f.type_count * 8;
    if (!within(f.types_offset, type_bytes, f.names_offset)) {
        return fail(err, err_size, "Truncated resource type list.");
    }
    size_t previous_end = type_bytes;
    for (size_t i = 0; i < f.type_count; i++) {
        const uint8_t *type = types + 2 + i * 8;
        size_t count = entries(type + 4);
        size_t ref_offset = be16(type + 6);
        size_t ref_bytes = count * 12;
        if (ref_offset < previous_end ||
            !within(ref_offset, ref_bytes, f.names_offset - f.types_offset)) {
            return fail(err, err_size, "Invalid or overlapping resource reference list.");
        }
        previous_end = ref_offset + ref_bytes;
        for (size_t j = 0; j < count; j++) {
            if (read_resource(&f, type, types + ref_offset + j * 12, NULL) != 0) {
                return fail(err, err_size, "Invalid resource payload or name bounds.");
            }
        }
        f.count += count;
    }
    *out = f;
    return 0;
}

size_t mac_resource_count(const MacResourceFork *fork) {
    return fork && fork->bytes ? fork->count : 0;
}

/* AppleSingle/AppleDouble container layout: RFC 1740, appendices A and B. */
int mac_resource_open_container(MacResourceFork *out, const void *bytes, size_t size,
                                char *err, size_t err_size) {
    const uint8_t *p = bytes;
    if (!p || size < 4 || (be32(p) != 0x00051607 && be32(p) != 0x00051600)) {
        return mac_resource_open(out, bytes, size, err, err_size);
    }
    if (err && err_size) {
        err[0] = 0;
    }
    if (!out) {
        return fail(err, err_size, "Missing resource-fork output.");
    }
    memset(out, 0, sizeof *out);
    if (size < 26 || be32(p + 4) != 0x00020000) {
        return fail(err, err_size, "Invalid or unsupported AppleDouble/AppleSingle header.");
    }
    size_t count = be16(p + 24);
    size_t header_size = 26 + count * 12;
    if (header_size > size) {
        return fail(err, err_size, "Truncated AppleDouble/AppleSingle entry table.");
    }
    size_t resource_offset = 0;
    size_t resource_size = 0;
    for (size_t i = 0; i < count; i++) {
        const uint8_t *entry = p + 26 + i * 12;
        size_t offset = be32(entry + 4);
        size_t length = be32(entry + 8);
        if (offset < header_size || !within(offset, length, size)) {
            return fail(err, err_size, "Invalid AppleDouble/AppleSingle entry bounds.");
        }
        if (be32(entry) == 2) {
            if (resource_offset) {
                return fail(err, err_size, "Duplicate AppleDouble/AppleSingle resource entry.");
            }
            resource_offset = offset;
            resource_size = length;
        }
    }
    if (!resource_offset) {
        return fail(err, err_size, "AppleDouble/AppleSingle has no resource fork.");
    }
    return mac_resource_open(out, p + resource_offset, resource_size, err, err_size);
}

int mac_resource_at(const MacResourceFork *f, size_t index, MacResource *out) {
    if (out) {
        memset(out, 0, sizeof *out);
    }
    if (!f || !f->bytes || !out) {
        return -1;
    }
    if (index >= f->count) {
        return 1;
    }
    const uint8_t *types = f->bytes + f->map_offset + f->types_offset;
    for (size_t i = 0; i < f->type_count; i++) {
        const uint8_t *type = types + 2 + i * 8;
        size_t count = entries(type + 4);
        if (index < count) {
            return read_resource(f, type, types + be16(type + 6) + index * 12, out);
        }
        index -= count;
    }
    return -1;
}

int mac_resource_find(const MacResourceFork *f, uint32_t type, int16_t id,
                      MacResource *out) {
    if (out) {
        memset(out, 0, sizeof *out);
    }
    if (!f || !f->bytes || !out) {
        return -1;
    }
    const uint8_t *types = f->bytes + f->map_offset + f->types_offset;
    for (size_t i = 0; i < f->type_count; i++) {
        const uint8_t *entry = types + 2 + i * 8;
        if (be32(entry) != type) {
            continue;
        }
        size_t count = entries(entry + 4);
        const uint8_t *refs = types + be16(entry + 6);
        for (size_t j = 0; j < count; j++) {
            if (be16(refs + j * 12) == (uint16_t)id) {
                return read_resource(f, entry, refs + j * 12, out);
            }
        }
    }
    return 1;
}
