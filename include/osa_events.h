#ifndef OS999_OSA_EVENTS_H
#define OS999_OSA_EVENTS_H

#include <stddef.h>
#include <stdint.h>
#include "ospi.h"

/* Four-character interface codes identify event suites, events and
 * descriptor types. They are interface identifiers, not copied protocol data. */
#define OSA_4CC(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

enum {
    OSA_CLASS_CORE = OSA_4CC('c', 'o', 'r', 'e'),
    OSA_CLASS_MISC = OSA_4CC('m', 'i', 's', 'c'),
};

enum {
    OSA_ID_GETD = OSA_4CC('g', 'e', 't', 'd'),
    OSA_ID_SETD = OSA_4CC('s', 'e', 't', 'd'),
    OSA_ID_SHUT = OSA_4CC('s', 'h', 'u', 't'),
};

enum {
    OSA_DESC_NULL = 0,
    OSA_DESC_TEXT = OSA_4CC('t', 'e', 'x', 't'),
    OSA_DESC_BOOL = OSA_4CC('b', 'o', 'o', 'l'),
    OSA_DESC_LONG = OSA_4CC('l', 'o', 'n', 'g'),
};

/* Our own error numbers; they are not legacy system error codes. */
enum {
    OSA_OK = 0,
    OSA_ERR_UNKNOWN_EVENT = -1010,
    OSA_ERR_UNKNOWN_PROPERTY = -1011,
    OSA_ERR_SET_FAILED = -1012,
    OSA_ERR_BAD_PARAMETER = -1013,
    OSA_ERR_HANDLER_FAILED = -1014,
    OSA_ERR_FULL = -1015,
};

/* Bounded typed descriptor: one text payload plus a numeric view. */
typedef struct {
    uint32_t type;
    char text[OSPI_NAME];
    long number;
} OsaDescriptor;

/* One scriptable message. The supported verbs carry their operands in the
 * named descriptor slots rather than a generic key-value list. */
typedef struct {
    uint32_t event_class;
    uint32_t event_id;
    char target[32];
    OsaDescriptor direct;
    OsaDescriptor property;
    OsaDescriptor value;
    int restart;
} AppleEvent;

typedef struct {
    int ok;
    long error_code;
    char message[192];
    OsaDescriptor result;
} AppleEventReply;

/* 0 on success; OSA_ERR_BAD_PARAMETER zeroes the event and leaves it unused. */
int osa_ae_init_getd(AppleEvent *e, const char *target, const char *property);
int osa_ae_init_setd(AppleEvent *e, const char *target, const char *property, const char *value);
int osa_ae_init_shut(AppleEvent *e, const char *target, int restart);
void osa_ae_error_reply(AppleEventReply *r, long code, const char *message);
int osa_ae_descriptor_text(const OsaDescriptor *d, char *out, size_t n);

#endif
