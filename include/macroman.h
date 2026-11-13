#ifndef MACROMAN_H
#define MACROMAN_H
/* Mac Roman <-> Unicode and the guest <-> host file-name mapping (src/macroman.c).
 *
 * The codepoint table is the standard published Mac Roman repertoire (the Unicode Consortium's MACINTOSH mapping, with $DB as the euro sign and
 * $F0 as the private-use codepoint U+F8FF that table assigns). Codepoints are facts; nothing here is Apple code or an Apple font.
 *
 * Name rules, shared by every place a guest name crosses the File Manager boundary:
 *  - A guest name is up to 31 Mac Roman BYTES per component (HFS), counted in Mac Roman, never in UTF-8.
 *  - Host names are UTF-8. Names this runtime writes are precomposed (NFC); names it reads may be NFC or NFD (combining marks after a base letter
 *    are composed before mapping), so both spellings of one name are the same name.
 *  - ':' is the guest path separator and cannot be in a guest component. A host name with ':' is therefore HIDDEN from the guest (never listed,
 *    never matched, never made). A '/' from the guest is refused (it would be a host path separator); '\\', NUL, control bytes ($00-$1F except CR ($0D), $7F)
 *    and a leading '.' are refused too, so the containment and traversal rules stay as they were.
 *  - A host name with a character outside Mac Roman, invalid UTF-8, or more than 31 Mac Roman bytes is HIDDEN from the guest as well.
 *  - Names match without regard to case, as HFS did: ASCII letters plus the accented Latin letters of Mac Roman (A-umlaut with a-umlaut, E-acute
 *    with e-acute, and so on; OE, AE, O-slash and Y-umlaut too). */
#include <stddef.h>
#include <stdint.h>

enum { MR_NAME_MAX = 31, MR_HOST_MAX = 3 * MR_NAME_MAX + 1 };   /* a Mac Roman byte is at most 3 UTF-8 bytes */

uint32_t mr_unicode(uint8_t byte);                 /* the codepoint of a Mac Roman byte (ASCII is itself) */
int mr_from_unicode(uint32_t cp);                  /* the Mac Roman byte of a codepoint, or -1 */
uint8_t mr_fold(uint8_t byte);                     /* lower case, by the classic Mac rule above */
int mr_name_byte_ok(uint8_t byte);                 /* may appear in a guest name component: $20-$7E, CR ($0D) and $80-$FF except ':' '/' '\\' */

/* Guest component (Mac Roman bytes, 1 to 31, no leading '.') -> host UTF-8 NFC. Returns the UTF-8 length, or -1 for a refused name. */
int mr_component_to_host(const uint8_t *mr, size_t n, char *out, size_t capacity);
/* Host component (UTF-8, NFC or NFD) -> Mac Roman. Returns the length (1 to 31), or -1 when the guest cannot see this name. */
int mr_component_from_host(const char *utf8, uint8_t *out, size_t capacity);
/* The Mac Roman length of a host component, or -1 (hidden). */
int mr_host_length(const char *utf8);
/* Whether two host names (UTF-8, any length, a leading dot allowed) are the same name without regard to case or normalisation; names the guest
 * cannot see compare byte for byte. */
int mr_host_equal_fold(const char *a, const char *b);
/* Whether a host name (any length, a leading dot allowed) is made only of characters the guest can name: the visibility test of volume counts. */
int mr_host_representable(const char *utf8);
/* A file that can carry a fork sidecar "<leaf>.rsrc": a legal guest name of up to 31 Mac Roman bytes. The sidecar's host name is up to 36 and is never
 * listed or addressable by the guest (names over 31 are hidden from it), so a 31-byte file name can have a resource fork. */
int mr_sidecar_ok(const char *leaf);
/* Whether a host name is a fork sidecar: a legal guest name followed by ".rsrc" (so up to 36 Mac Roman bytes in all). */
int mr_is_sidecar(const char *host);
int mr_equal_fold(const uint8_t *a, size_t an, const uint8_t *b, size_t bn);
/* Writes a Pascal string of at most `max` bytes to dst (dst must hold max+1). A hidden name writes an empty string and returns -1. */
int mr_put_pstring(uint8_t *dst, size_t max, const char *utf8);
#endif
