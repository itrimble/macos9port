/* Apple Event records for the OSA scripting layer. */
#include "osa_events.h"

#include <string.h>

static int copy_bounded(char *out, size_t n, const char *in) {
    if (!in || !out || n == 0) return OSA_ERR_BAD_PARAMETER;
    size_t len = strnlen(in, n);
    if (len >= n) return OSA_ERR_BAD_PARAMETER;
    memcpy(out, in, len + 1);
    return OSA_OK;
}

int osa_ae_init_getd(AppleEvent *e, const char *target, const char *property) {
    if (!e) return OSA_ERR_BAD_PARAMETER;
    memset(e, 0, sizeof *e);
    int err = copy_bounded(e->target, sizeof e->target, target);
    if (err) return err;
    err = copy_bounded(e->property.text, sizeof e->property.text, property);
    if (err) { memset(e, 0, sizeof *e); return err; }
    e->event_class = OSA_CLASS_CORE;
    e->event_id = OSA_ID_GETD;
    e->property.type = OSA_DESC_TEXT;
    return OSA_OK;
}

int osa_ae_init_setd(AppleEvent *e, const char *target, const char *property, const char *value) {
    if (!e) return OSA_ERR_BAD_PARAMETER;
    int err = osa_ae_init_getd(e, target, property);
    if (err) return err;
    err = copy_bounded(e->value.text, sizeof e->value.text, value);
    if (err) {
        memset(e, 0, sizeof *e);
        return err;
    }
    e->event_id = OSA_ID_SETD;
    e->value.type = OSA_DESC_TEXT;
    return OSA_OK;
}

int osa_ae_init_shut(AppleEvent *e, const char *target, int restart) {
    if (!e) return OSA_ERR_BAD_PARAMETER;
    memset(e, 0, sizeof *e);
    int err = copy_bounded(e->target, sizeof e->target, target);
    if (err) {
        memset(e, 0, sizeof *e);
        return err;
    }
    e->event_class = OSA_CLASS_MISC;
    e->event_id = OSA_ID_SHUT;
    e->direct.type = OSA_DESC_BOOL;
    e->direct.number = restart ? 1 : 0;
    e->restart = restart ? 1 : 0;
    return OSA_OK;
}

void osa_ae_error_reply(AppleEventReply *r, long code, const char *message) {
    if (!r) return;
    memset(r, 0, sizeof *r);
    r->ok = 0;
    r->error_code = code;
    if (message) {
        size_t len = strnlen(message, sizeof r->message);
        if (len == sizeof r->message) len = sizeof r->message - 1;
        memcpy(r->message, message, len);
        r->message[len] = 0;
    }
}

int osa_ae_descriptor_text(const OsaDescriptor *d, char *out, size_t n) {
    if (!d || d->type != OSA_DESC_TEXT || !out || n == 0) return OSA_ERR_BAD_PARAMETER;
    if (!memchr(d->text, 0, sizeof d->text)) return OSA_ERR_BAD_PARAMETER;
    return copy_bounded(out, n, d->text);
}
