#ifndef ST_SIT_H
#define ST_SIT_H

#include "st_int.h"

const char *st_sit_method_name(int method);
/* Decompress one fork. crc_kind 0 skips the check, 1 = CRC-16/ARC. */
int st_sit_unpack(int method, const uint8_t *p, size_t n, size_t want, uint16_t crc, int crc_kind,
                  StBytes *out, const StLimits *lim, const char *what, StErr *err);

#endif
