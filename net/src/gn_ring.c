/* A bounded byte ring: the buffer shape every connection in the broker and
 * every endpoint on the personal area network uses. */
#include "gn_ring.h"

#include <stdlib.h>

int gn_ring_init(GnRing *r, size_t cap) {
    r->p = malloc(cap);
    if (!r->p) return GN_ENOMEM;
    r->cap = cap;
    r->head = r->len = 0;
    return GN_OK;
}

void gn_ring_free(GnRing *r) {
    free(r->p);
    r->p = NULL;
    r->cap = r->head = r->len = 0;
}

size_t gn_ring_put(GnRing *r, const uint8_t *p, size_t n) {
    size_t room = r->cap - r->len, k = n < room ? n : room;
    for (size_t i = 0; i < k; i++) r->p[(r->head + r->len + i) % r->cap] = p[i];
    r->len += k;
    return k;
}

size_t gn_ring_get(GnRing *r, uint8_t *p, size_t n) {
    size_t k = r->len < n ? r->len : n;
    for (size_t i = 0; i < k; i++) p[i] = r->p[(r->head + i) % r->cap];
    r->head = (r->head + k) % r->cap;
    r->len -= k;
    return k;
}

size_t gn_ring_peek(GnRing *r, uint8_t *p, size_t n) {
    size_t k = r->len < n ? r->len : n;
    for (size_t i = 0; i < k; i++) p[i] = r->p[(r->head + i) % r->cap];
    return k;
}

void gn_ring_drop(GnRing *r, size_t n) {
    size_t k = r->len < n ? r->len : n;
    r->head = (r->head + k) % r->cap;
    r->len -= k;
}
