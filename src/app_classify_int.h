#ifndef OS999_APP_CLASSIFY_INT_H
#define OS999_APP_CLASSIFY_INT_H

#include "app_classify.h"

typedef enum { APP_EXEC_ABSENT, APP_EXEC_OK, APP_EXEC_DAMAGED } AppExecResult;

/* ABSENT: the magic is not there. DAMAGED: the magic is there but the structure is inconsistent. */
AppExecResult app_exec_pef(const uint8_t *p, size_t n, AppClassification *c);
AppExecResult app_exec_cfrg(const uint8_t *p, size_t n, AppClassification *c);
AppExecResult app_exec_macho(const uint8_t *p, size_t n, AppClassification *c);
uint64_t app_hash_resource(uint32_t type, int16_t id, const uint8_t *data, size_t size);

#endif
