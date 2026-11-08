/* A bounded byte ring: the buffer shape every connection in the broker and
 * every endpoint on the personal area network uses. */
#ifndef GN_RING_H
#define GN_RING_H

#include "os999_guestnet.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *p;
    size_t cap, head, len;
} GnRing;

int gn_ring_init(GnRing *r, size_t cap);
void gn_ring_free(GnRing *r);
size_t gn_ring_put(GnRing *r, const uint8_t *p, size_t n);
size_t gn_ring_get(GnRing *r, uint8_t *p, size_t n);
size_t gn_ring_peek(GnRing *r, uint8_t *p, size_t n);
void gn_ring_drop(GnRing *r, size_t n);

#endif
