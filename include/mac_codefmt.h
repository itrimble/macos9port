#ifndef MAC_CODEFMT_H
#define MAC_CODEFMT_H

#include <stddef.h>
#include <stdint.h>

/* Pure readers for two byte layouts shared by the classifier and the runtime loader, so that a program one accepts is not one the other refuses.
 * No allocation, no I/O. Written from the public resource layouts (CODE 0 jump-table header; the extended resource header of compressed resources). */

/* CODE 0: above-A5 size, below-A5 size, jump-table size, jump-table offset (four longs), then the table in 8-byte entries.
 * An installer that loads itself in stages may ship a CODE 0 that holds fewer entries than it declares (the table is filled in by the stub before any
 * other entry is used); `present` is what is actually stored and `truncated` says it is less than declared. */
typedef struct { uint32_t above, below, length, offset, present; int truncated; } MacCode0;

static inline uint32_t mac_cf_be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline uint32_t mac_cf_be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

/* 0: usable layout; -1: not. At least one whole entry must be stored, and everything declared must lie inside the A5 world. */
static inline int mac_code0_parse(const uint8_t *d, size_t n, MacCode0 *out) {
    if (!d || n < 24) return -1;
    MacCode0 c = {mac_cf_be32(d), mac_cf_be32(d + 4), mac_cf_be32(d + 8), mac_cf_be32(d + 12), 0, 0};
    if (c.below > 0x100000 || c.above > 0x100000 || c.length < 8 || c.length % 8 || (c.offset & 1) ||
        c.offset > c.above || c.length > c.above - c.offset) return -1;
    size_t room = (n - 16) & ~(size_t)7;
    c.present = room < c.length ? (uint32_t)room : c.length;
    c.truncated = c.present < c.length;
    if (c.present < 8) return -1;
    if (out) *out = c;
    return 0;
}

/* Compressed resources. Two header forms are recognised.
 *  MAC_COMP_SYSTEM: the Resource Manager's extended header: signature A89F 6572, header length (word), header version (8 or 9), attributes, expanded size
 *    (long), then for version 8 a working-buffer fraction byte, an expansion-buffer byte, the decompressor 'dcmp' ID (word) and a reserved word. The body
 *    follows the header and is expanded by a 'dcmp' resource.
 *  MAC_COMP_MARKED: a resource whose first long is A89F 000C. Its layout belongs to a program's own packer (an installer engine); the Resource Manager does
 *    not expand it. The word pair at +8/+12 is read here only as "expanded size, stored size" for loader checks; the program's patched load hook does the work. */
enum { MAC_COMP_NONE = 0, MAC_COMP_SYSTEM = 1, MAC_COMP_MARKED = 2 };
typedef struct { int kind; unsigned version, header_len; uint8_t attributes, work_fraction, expand_extra; int16_t dcmp_id; uint32_t expanded; } MacComp;

#define MAC_COMP_MAX_EXPANDED UINT32_C(0x4000000)   /* 64 MB: a larger claim is a damaged header, not a resource */

/* 0: not compressed (`out` cleared); 1: compressed, header understood; -1: carries a compression signature but the header is damaged or of a version
 * this build does not know. */
static inline int mac_comp_parse(const uint8_t *d, size_t n, MacComp *out) {
    MacComp c = {0};
    if (out) *out = c;
    if (!d || n < 4 || d[0] != 0xa8 || d[1] != 0x9f) return 0;
    if (d[2] == 0x65 && d[3] == 0x72) {
        c.kind = MAC_COMP_SYSTEM;
        if (n < 12) return -1;
        c.header_len = mac_cf_be16(d + 4); c.version = d[6]; c.attributes = d[7]; c.expanded = mac_cf_be32(d + 8);
        if (c.version != 8 && c.version != 9) return -1;
        if (c.header_len < 18 || c.header_len > n || c.expanded > MAC_COMP_MAX_EXPANDED) return -1;
        if (c.version == 8) { c.work_fraction = d[12]; c.expand_extra = d[13]; c.dcmp_id = (int16_t)mac_cf_be16(d + 14); }
        else c.dcmp_id = (int16_t)mac_cf_be16(d + 12);
        if (out) *out = c;
        return 1;
    }
    if (d[2] == 0x00 && d[3] == 0x0c) {
        if (n < 16) return 0;
        c.kind = MAC_COMP_MARKED; c.expanded = mac_cf_be32(d + 8);
        if (c.expanded > MAC_COMP_MAX_EXPANDED) return 0;
        if (out) *out = c;
        return 1;
    }
    return 0;
}

/* The size the program will see once the resource is loaded: the expanded size of either form, else the stored size. */
static inline size_t mac_comp_size(const uint8_t *d, size_t n) {
    MacComp c;
    return mac_comp_parse(d, n, &c) == 1 && c.expanded ? c.expanded : n;
}

#endif
