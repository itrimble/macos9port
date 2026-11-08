#ifndef OS999_OSA_BRIDGE_H
#define OS999_OSA_BRIDGE_H

#include <stddef.h>
#include "osa_events.h"

/* Flat bridge view of one event; the JSON form travels between scripts,
 * IPC clients and the desktop backend. */
typedef struct {
    uint32_t event_class;
    uint32_t event_id;
    char target[32];
    char property[OSPI_NAME];
    char value[OSPI_NAME];
    int restart;
} OsaBridgeRequest;

/* code is carried in memory; the JSON form keeps ok/value/error only. */
typedef struct {
    int ok;
    char value[OSPI_NAME];
    char error[192];
    long code;
} OsaBridgeReply;

/* Handler surface a host implements; any pointer may be NULL. */
typedef struct {
    int (*get)(void *ctx, const char *property, char *out, size_t out_size);
    int (*set)(void *ctx, const char *property, const char *value, char *err, size_t err_size);
    int (*system)(void *ctx, int restart, char *err, size_t err_size);
    void *ctx;
} OsaBridgeBackend;

int osa_ae_to_request(const AppleEvent *e, OsaBridgeRequest *q);
int osa_request_to_json(const OsaBridgeRequest *q, char *json, size_t n);
int osa_json_to_request(const char *json, OsaBridgeRequest *q);
int osa_reply_to_json(const OsaBridgeReply *r, char *json, size_t n);
int osa_json_to_reply(const char *json, OsaBridgeReply *r);
void osa_request_to_ae_reply(const OsaBridgeReply *r, AppleEventReply *out);

/* Routes one request through the backend and fills the reply. */
int osa_bridge_dispatch(const OsaBridgeRequest *q, const OsaBridgeBackend *backend, OsaBridgeReply *r);

/* Event -> request -> dispatch -> reply in one call. */
int osa_ae_send(const AppleEvent *e, const OsaBridgeBackend *backend, AppleEventReply *out);

#endif
