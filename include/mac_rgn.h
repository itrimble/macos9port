#ifndef MAC_RGN_INCLUDED
#define MAC_RGN_INCLUDED
#include <stddef.h>
#include <stdint.h>

/* A QuickDraw region as a canonical list of rectangles: non-overlapping, sorted by top then left, touching runs in a band merged and
 * identical consecutive bands merged vertically, so two regions covering the same pixels have identical lists. Edges are top/left
 * inclusive and bottom/right exclusive. The guest's encoded form (mac_rgn_decode / mac_rgn_encode) is the documented one: rgnSize,
 * rgnBBox, then scan-line records (v, ascending h inversion points, $7FFF) whose points toggle the row above, a bottom record that
 * cancels the last row, and a final $7FFF; a rectangle or an empty region is just the 10-byte header. Decoding treats the bytes as
 * untrusted input. */
typedef struct { int t, l, b, r; } MacRect;
typedef struct { MacRect *r; int n, cap; } MacRgn;
enum { MAC_RGN_MAX_RECTS = 2048, MAC_RGN_MAX_BYTES = 32760, MAC_RGN_OP_SECT = 0, MAC_RGN_OP_UNION = 1, MAC_RGN_OP_DIFF = 2, MAC_RGN_OP_XOR = 3 };

void mac_rgn_init(MacRgn *g);
void mac_rgn_free(MacRgn *g);
int mac_rgn_set_rect(MacRgn *g, MacRect r);                       /* an empty rectangle makes an empty region; 0 ok, -1 out of memory */
int mac_rgn_copy(MacRgn *dst, const MacRgn *src);
/* Parses an encoded region of exactly `size` bytes (big-endian). 0 ok, -1 malformed or too large, with *why naming the reason. */
int mac_rgn_decode(const uint8_t *data, size_t size, MacRgn *out, const char **why);
/* Encoded size in bytes (10 for a rectangle or an empty region), or 0 if it would exceed MAC_RGN_MAX_BYTES. */
size_t mac_rgn_encoded_size(const MacRgn *g);
/* Writes the encoding into out (capacity cap, at least mac_rgn_encoded_size). Returns the byte count, or 0 on failure. */
size_t mac_rgn_encode(const MacRgn *g, uint8_t *out, size_t cap);
/* out = a OP b. out may not alias a or b. 0 ok, -1 failure with *why (too many rectangles, or out of memory). */
int mac_rgn_op(int op, const MacRgn *a, const MacRgn *b, MacRgn *out, const char **why);
int mac_rgn_contains(const MacRgn *g, int x, int y);
int mac_rgn_rect_in(const MacRgn *g, MacRect q);                  /* does any pixel of q lie in the region */
int mac_rgn_is_empty(const MacRgn *g);
int mac_rgn_is_rect(const MacRgn *g, MacRect *r);                 /* exactly one rectangle */
void mac_rgn_bbox(const MacRgn *g, MacRect *r);                   /* all zero for an empty region */
int mac_rgn_equal(const MacRgn *a, const MacRgn *b);
void mac_rgn_offset(MacRgn *g, int dh, int dv);
#endif
