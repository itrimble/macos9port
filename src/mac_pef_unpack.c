#include "mac_pef.h"

#include <stdio.h>
#include <string.h>

/* Pattern-initialized data: a byte stream of instructions. Each starts with a
 * byte holding a three-bit opcode and a five-bit count; a zero count means the
 * real count follows as a variable-length argument (seven bits per byte, most
 * significant group first, high bit set on every byte but the last). */

typedef struct {
    const uint8_t *in, *in_end;
    uint8_t *out;
    uint64_t produced, capacity;
} Stream;

static int argument(Stream *s, uint32_t *value) {
    uint32_t v = 0;
    for (int i = 0; i < 5; i++) {
        if (s->in >= s->in_end || (v >> 25)) return -1;
        uint8_t b = *s->in++;
        v = (v << 7) | (b & 0x7fu);
        if (!(b & 0x80u)) { *value = v; return 0; }
    }
    return -1;
}

static int emit(Stream *s, const uint8_t *bytes, uint32_t count) { /* bytes == NULL writes zeros */
    if (count > s->capacity - s->produced) return -1;
    if (!count) return 0;
    if (bytes) memcpy(s->out + s->produced, bytes, count);
    else memset(s->out + s->produced, 0, count);
    s->produced += count;
    return 0;
}

static const uint8_t *take(Stream *s, uint64_t count) {
    if (count > (uint64_t)(s->in_end - s->in)) return NULL;
    const uint8_t *p = s->in;
    s->in += count;
    return p;
}

long mac_pef_pattern_expand(const uint8_t *packed, size_t packed_size, uint8_t *out, size_t out_size) {
    if ((!packed && packed_size) || (!out && out_size) || out_size > (size_t)MAC_PEF_SECTION_LIMIT) return -1;
    Stream s = {packed, packed + packed_size, out, 0, out_size};
    while (s.in < s.in_end) {
        uint8_t head = *s.in++;
        uint32_t count = head & 0x1fu, custom = 0, repeat = 0;
        if (!count && argument(&s, &count)) return -1;
        switch (head >> 5) {
        case 0: /* zero: count zero bytes */
            if (emit(&s, NULL, count)) return -1;
            break;
        case 1: { /* block copy: count literal bytes */
            const uint8_t *block = take(&s, count);
            if (!block || emit(&s, block, count)) return -1;
            break;
        }
        case 2: { /* repeated block: one block of count bytes, written repeat + 1 times */
            if (argument(&s, &repeat)) return -1;
            const uint8_t *block = take(&s, count);
            if (!block || (uint64_t)count * ((uint64_t)repeat + 1) > s.capacity - s.produced) return -1;
            for (uint64_t i = 0; count && i <= repeat; i++) emit(&s, block, count);
            break;
        }
        case 3:   /* interleave repeat with block copy: common, custom 1, common, ..., custom N, common */
        case 4: { /* interleave repeat with zero: the common part is count zero bytes */
            if (argument(&s, &custom) || argument(&s, &repeat)) return -1;
            const uint8_t *common = (head >> 5) == 3 ? take(&s, count) : NULL;
            if ((head >> 5) == 3 && !common) return -1;
            const uint8_t *customs = take(&s, (uint64_t)custom * repeat);
            if (!customs) return -1;
            if ((uint64_t)count * ((uint64_t)repeat + 1) + (uint64_t)custom * repeat > s.capacity - s.produced) return -1;
            for (uint64_t i = 0; (count || custom) && i < repeat; i++) {
                emit(&s, common, count);
                emit(&s, customs + i * custom, custom);
            }
            emit(&s, common, count);
            break;
        }
        default:
            return -1;
        }
    }
    return (long)s.produced;
}

int mac_pef_unpack(const MacPef *pef, unsigned section, uint8_t *out, size_t out_size, char *err, size_t n) {
    if (!pef || !out || section >= pef->section_count) {
        if (err && n) snprintf(err, n, "Invalid PEF section request.");
        return -1;
    }
    const MacPefSection *s = &pef->sections[section];
    if (!mac_pef_instantiated(s->kind) || out_size < s->total_size) {
        if (err && n) snprintf(err, n, "PEF section %u cannot be unpacked into %zu bytes.", section, out_size);
        return -1;
    }
    const uint8_t *packed = pef->bytes + s->container_offset;
    memset(out, 0, s->total_size);
    if (s->kind != MAC_PEF_PATTERN_DATA) {
        memcpy(out, packed, s->packed_size); /* mac_pef_open checked packed <= unpacked <= total */
        return 0;
    }
    if (mac_pef_pattern_expand(packed, s->packed_size, out, s->unpacked_size) < 0) {
        if (err && n) snprintf(err, n, "PEF section %u has malformed pattern-initialized data.", section);
        return -1;
    }
    return 0;
}
