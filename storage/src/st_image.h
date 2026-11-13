#ifndef ST_IMAGE_H
#define ST_IMAGE_H

#include "st_int.h"

int st_udif_decode(const uint8_t *p, size_t n, StBytes *raw, const StLimits *lim, StErr *err);
int st_explain_short_hfs(const uint8_t *vol, size_t len, StErr *err);
/* Find a resource in a raw resource fork; 0 when found. */
int st_find_resource(const uint8_t *fork, size_t n, uint32_t type, int16_t id, const uint8_t **data,
                     size_t *size);

#endif
